"""Build against a pinned upstream AMD64 host and its exact wxWidgets SDK.

Generate import libraries from that host's actual export tables rather than
reusing the x86 libraries shipped in opencpn-libs. The host is only extracted,
never installed, so the executor's existing OpenCPN profile is unaffected.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
WX_VERSION = os.environ.get('CLIMATOLOGY_WX_VERSION', '3.3.4')
SDK_FILES = {'3.3.4': 'windows64-sdk.json', '3.2.9': 'windows64-wx329-sdk.json'}
if WX_VERSION not in SDK_FILES:
    raise ValueError('Unsupported Windows wxWidgets SDK: ' + WX_VERSION)
WORK = ROOT / ('.windows64' if WX_VERSION == '3.3.4' else '.windows64-wx329')
ARTIFACTS = ROOT / 'artifacts' / ('windows-x64' if WX_VERSION == '3.3.4' else 'windows-x64-wx329')

from windows_abi import validate as validate_windows_abi


def run(*args, cwd=ROOT):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(url, path, expected):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        partial = path.with_suffix(path.suffix + '.part')
        with urllib.request.urlopen(url, timeout=120) as response, partial.open('wb') as output:
            shutil.copyfileobj(response, output)
        if digest(partial) != expected:
            raise RuntimeError(f'Checksum mismatch: {path.name}')
        partial.replace(path)
    if digest(path) != expected:
        raise RuntimeError(f'Checksum mismatch: {path.name}')


def pe_sections(path):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError(f'Not a PE image: {path}')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if data[offset:offset + 4] != b'PE\0\0':
        raise ValueError(f'Invalid PE signature: {path}')
    machine, count = struct.unpack_from('<HH', data, offset + 4)
    optional_size = struct.unpack_from('<H', data, offset + 20)[0]
    magic = struct.unpack_from('<H', data, offset + 24)[0]
    if machine != 0x8664 or magic != 0x20b:
        raise ValueError(f'Expected AMD64 PE32+: {path}')
    sections = []
    for index in range(count):
        at = offset + 24 + optional_size + index * 40
        size, rva = struct.unpack_from('<II', data, at + 8)
        flags = struct.unpack_from('<I', data, at + 36)[0]
        sections.append((rva, rva + size, bool(flags & 0x20000000)))
    return sections


def exports(path):
    sections = pe_sections(path)
    text = subprocess.check_output(['dumpbin', '/exports', str(path)], text=True)
    result = {}
    for line in text.splitlines():
        match = re.match(r'\s*\d+\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s+(\S+)', line)
        if match:
            rva = int(match[1], 16)
            code = any(start <= rva < end and executable for start, end, executable in sections)
            result[match[2]] = '' if code else ' DATA'
    if not result:
        raise RuntimeError(f'No named exports: {path}')
    return result


def import_library(image, destination):
    names = exports(image)
    definition = destination.with_suffix('.def')
    definition.write_text('LIBRARY ' + image.name + '\nEXPORTS\n' +
                          '\n'.join(name + kind for name, kind in sorted(names.items())) + '\n')
    run('lib', '/nologo', '/machine:x64', '/def:' + str(definition), '/out:' + str(destination))
    return names


def prepare_sdk():
    pin = json.loads((ROOT / 'ci/public-packages' / SDK_FILES[WX_VERSION]).read_text())
    cache = WORK / 'cache'
    assert pin['wx_version'] == WX_VERSION
    host_archive = cache / 'opencpn-x64.exe'
    download(pin['host_url'], host_archive, pin['host_sha256'])
    host = WORK / 'host'
    run('7z', 'x', '-y', '-o' + str(host), host_archive)
    sdk = WORK / 'sdk'
    sdk.mkdir(parents=True, exist_ok=True)
    host_exports = import_library(host / 'opencpn.exe', sdk / 'opencpn.lib')
    import_library(host / 'z.dll', sdk / 'z.lib')
    wx = WORK / ('wxWidgets-' + WX_VERSION)
    for filename, expected in pin['wx_archives'].items():
        path = cache / filename
        download('https://github.com/wxWidgets/wxWidgets/releases/download/v' + WX_VERSION + '/' + filename,
                 path, expected)
        run('7z', 'x', '-y', '-o' + str(wx), path)
    wxlib = wx / 'lib/vc14x_x64_dll'
    for image in wxlib.glob('*.dll'):
        pe_sections(image)
        # CI tests use the same wx runtime bytes as the matched upstream host.
        counterpart = host / image.name
        if counterpart.is_file() and digest(image) != digest(counterpart):
            raise RuntimeError(f'Host/wxWidgets runtime mismatch: {image.name}')
    os.environ['PATH'] = str(host) + os.pathsep + str(wxlib) + os.pathsep + os.environ['PATH']
    os.environ['WX_VER'] = '32' if WX_VERSION == '3.2.9' else '33'
    os.environ['OCPN_TARGET'] = 'MSVC-x64'
    (ARTIFACTS / 'sdk-provenance.json').write_text(json.dumps(pin, indent=2) + '\n')
    return sdk, wx, wxlib, host_exports


def verify_host_imports(library, host_exports):
    pe_sections(library)
    text = subprocess.check_output(['dumpbin', '/imports', str(library)], text=True)
    (ARTIFACTS / 'plugin-imports.txt').write_text(text)
    validate_windows_abi(library.read_bytes(), WX_VERSION)
    dependencies = {}
    active = None
    for line in text.splitlines():
        if line.strip().lower().endswith(('.dll', '.exe')):
            active = line.strip().lower()
            dependencies.setdefault(active, [])
        elif active:
            match = re.match(r'\s*[0-9A-Fa-f]+\s+(\S+)', line)
            if match and match[1].startswith('?'):
                dependencies[active].append(match[1])
    names = dependencies.get('opencpn.exe', [])
    if not names or set(names) - host_exports.keys():
        raise RuntimeError('Unresolved OpenCPN imports: ' + str(sorted(set(names) - host_exports.keys())))
    for name, symbols in dependencies.items():
        if name.startswith('wx'):
            missing = set(symbols) - exports(WORK / 'host' / name).keys()
            if missing:
                raise RuntimeError('Unresolved wxWidgets imports in ' + name + ': ' + str(sorted(missing)))
    print(f'All {len(names)} OpenCPN imports and wxWidgets {WX_VERSION} imports resolve', flush=True)



def main():
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    sdk, wx, wxlib, host_exports = prepare_sdk()
    work = WORK / 'build'
    run('cmake', '-S', ROOT, '-B', work, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DCMAKE_SYSTEM_VERSION=10.0', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=ON',
        '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        '-DwxWidgets_ROOT_DIR=' + str(wx), '-DwxWidgets_LIB_DIR=' + str(wxlib),
        '-DwxWidgets_CONFIGURATION=mswu',
        '-DCLIMATOLOGY_WINDOWS_IMPORT_LIBRARY=' + str(sdk / 'opencpn.lib'),
        '-DCLIMATOLOGY_WINDOWS_ZLIB_LIBRARY=' + str(sdk / 'z.lib'))
    run('cmake', '--build', work, '--config', 'Release', '--parallel', '3')
    run('ctest', '--test-dir', work, '-C', 'Release', '--output-on-failure')
    library = work / 'Release/climatology_pi.dll'
    verify_host_imports(library, host_exports)
    run(sys.executable, ROOT / 'ci/public-packages/package.py', '--binary', library,
        '--build-dir', work, '--kind', 'windows', '--target', 'msvc-64',
        '--target-version', '10', '--arch', 'x86_64', '--wx-version', WX_VERSION)
    package_dir = ROOT / ('public-packages' if WX_VERSION == '3.3.4' else 'compatibility/windows-x64-wx329')
    archive = next(package_dir.glob('*.tar.gz'))
    run(sys.executable, ROOT / 'ci/public-packages/windows-host.py', archive, WORK / 'host')

if __name__ == '__main__':
    main()
