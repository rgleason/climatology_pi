#!/usr/bin/env python3
"""Build complete OpenCPN import tarballs from a platform-native plugin library."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import xml.etree.ElementTree as ET

from windows_abi import validate as validate_windows_abi

VERSION = '2.0.1.0'
TAG = 'v2.0.1.0-modern-data'
REPO = 'https://github.com/pob220/climatology_pi'
BASELINE = 'cd00282e6ea2784a6d78ccfe47fed713269ad87e'
SOURCE = Path(__file__).resolve().parents[2]
EPOCH = 1792540800


def digest(data):
    return hashlib.sha256(data).hexdigest()


def make_package(args):
    binary = args.binary.read_bytes()
    packaging_revision = subprocess.check_output(['git', '-C', str(SOURCE), 'rev-parse', 'HEAD'], text=True).strip()
    revision = args.binary_source_revision or packaging_revision
    subprocess.run(['git', '-C', str(SOURCE), 'cat-file', '-e', revision + '^{commit}'], check=True)
    if binary[:4] == b'\x7fELF':
        machine = struct.unpack_from('<H', binary, 18)[0]
        expected = {'x86_64': 62, 'arm64': 183, 'aarch64': 183, 'armhf': 40}
        assert machine == expected[args.arch], (machine, args.arch)
        if args.kind == 'android':
            if binary[4] == 2:
                offset = struct.unpack_from('<Q', binary, 32)[0]
                size, count = struct.unpack_from('<HH', binary, 54)
                align_offset, align_format = 48, '<Q'
            else:
                offset = struct.unpack_from('<I', binary, 28)[0]
                size, count = struct.unpack_from('<HH', binary, 42)
                align_offset, align_format = 28, '<I'
            loads = [offset + i * size for i in range(count)
                     if struct.unpack_from('<I', binary, offset + i * size)[0] == 1]
            assert loads and all(struct.unpack_from(align_format, binary, p + align_offset)[0] >= 16384 for p in loads), 'Android LOAD alignment is below 16 KiB'
    elif binary[:2] == b'MZ':
        pe = struct.unpack_from('<I', binary, 0x3c)[0]
        machine = struct.unpack_from('<H', binary, pe + 4)[0]
        assert machine == {'x86': 0x14c, 'x86_64': 0x8664}[args.arch]
        if args.arch == 'x86_64':
            assert args.wx_version, 'Windows x64 packages require an explicit wxWidgets version'
            validate_windows_abi(binary, args.wx_version)
    elif args.kind != 'macos':
        raise ValueError('Unrecognised native library format')
    manifest = json.loads((SOURCE / 'data/dataset-manifest.json').read_text())
    assert manifest['dataset_version'] == 'ocpn-climatology-2026.2'
    for output in manifest['outputs']:
        content = (SOURCE / 'data' / output['file']).read_bytes()
        assert len(content) == output['bytes'], output['file']
        assert digest(content) == output['sha256'], output['file']
    label = f'{args.target}-{args.target_version}-{args.arch.replace(";", "-")}'
    if args.kind == 'windows' and args.arch == 'x86_64':
        label += '-wx' + args.wx_version.replace('.', '')
    name = f'climatology_pi-{VERSION}_{label}'
    url = f'{REPO}/releases/download/{TAG}/{name}.tar.gz'
    plugin = ET.Element('plugin', version='1')
    values = [
        ('name', 'Climatology'), ('version', VERSION), ('release', '0'),
        ('summary', 'Global monthly climate and cyclone data with source provenance'),
        ('api-version', args.api_version), ('open-source', 'yes'),
        ('author', "Sean D'Epagnier; modernisation contributors"),
        ('source', f'{REPO}/tree/{revision}'),
        ('description', 'Monthly ERA5 winds and atmosphere, OSCAR currents, OISST sea temperature, '
         'IBTrACS cyclones and GEBCO bathymetric context. Includes the complete '
         'ocpn-climatology-2026.2 dataset, source periods, checksums and validation. '
         'Historical LIS/OTD lightning is retained with its provenance limitation. '
         'Research and passage planning climatology; not a forecast or official navigation data.'),
        ('target', args.target), ('target-version', args.target_version),
        ('target-arch', args.arch), ('tarball-url', url),
        ('info-url', f'{REPO}/releases/tag/{TAG}'),
    ]
    for key, value in values:
        ET.SubElement(plugin, key).text = value
    ET.indent(plugin)
    # OpenCPN wraps embedded metadata in <plugins>, so omit an XML declaration.
    metadata = ET.tostring(plugin, encoding='utf-8') + b'\n'
    entries = {'metadata.xml': metadata}
    if args.kind == 'windows':
        library_path = 'plugins/climatology_pi.dll'
        data_root = 'plugins/climatology_pi'
        locale_root = 'share/locale'
    elif args.kind == 'macos':
        library_path = 'OpenCPN.app/Contents/PlugIns/libclimatology_pi.dylib'
        data_root = 'OpenCPN.app/Contents/SharedSupport/plugins/climatology_pi'
        locale_root = 'OpenCPN.app/Contents/Resources'
    else:
        library_path = 'lib/opencpn/libclimatology_pi.so'
        data_root = 'share/opencpn/plugins/climatology_pi'
        locale_root = 'share/locale'
    entries[library_path] = binary
    for directory in ('data', 'UserIcons'):
        for path in sorted((SOURCE / directory).rglob('*')):
            if path.is_file():
                assert not path.is_symlink(), path
                entries[f'{data_root}/{directory}/{path.relative_to(SOURCE / directory).as_posix()}'] = path.read_bytes()
    entries[f'{data_root}/data/COPYING'] = (SOURCE / 'cmake/gpl.txt').read_bytes()
    for mo in sorted(args.build_dir.glob('**/opencpn-climatology_pi.mo')):
        locale = mo.parent.name.removesuffix('.lproj')
        if locale == 'LC_MESSAGES':
            locale = mo.parent.parent.name
        destination = (f'{locale_root}/{locale}.lproj/opencpn-climatology_pi.mo' if args.kind == 'macos'
                       else f'{locale_root}/{locale}/LC_MESSAGES/opencpn-climatology_pi.mo')
        entries[destination] = mo.read_bytes()
    provenance = dict(schema='climatology-plugin-package-v1', plugin_version=VERSION,
                      dataset_version=manifest['dataset_version'], baseline_revision=BASELINE,
                      source_revision=revision, packaging_revision=packaging_revision, target=args.target,
                      target_version=args.target_version, architecture=args.arch,
                      binary_sha256=digest(binary), dataset_outputs_verified=len(manifest['outputs']))
    if args.kind == 'windows' and args.arch == 'x86_64':
        provenance['wx_version'] = args.wx_version
    assert sum(n.endswith('.mo') for n in entries) >= 30, 'Translation catalogs are missing'
    entries[f'{data_root}/data/package-provenance.json'] = (json.dumps(provenance, indent=2) + '\n').encode()
    if args.output is None:
        args.output = Path('compatibility/windows-x64-wx329' if
                           args.kind == 'windows' and args.arch == 'x86_64' and
                           args.wx_version == '3.2.9' else 'public-packages')
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / f'{name}.tar.gz'
    with archive.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=EPOCH) as compressed:
        with tarfile.open(fileobj=compressed, mode='w', format=tarfile.PAX_FORMAT) as tar:
            for path, content in sorted(entries.items()):
                info = tarfile.TarInfo(f'{name}/{path}')
                info.size = len(content)
                info.mtime = EPOCH
                info.mode = 0o755 if path == library_path else 0o644
                tar.addfile(info, io.BytesIO(content))
    # External metadata can carry the archive digest; embedded metadata cannot hash itself.
    checksum = ET.Element('tarball-checksum')
    checksum.text = digest(archive.read_bytes())
    plugin.insert(list(plugin).index(plugin.find('info-url')), checksum)
    ET.indent(plugin)
    xml = args.output / f'{name}.xml'
    xml.write_bytes(ET.tostring(plugin, encoding='utf-8') + b'\n')
    with tarfile.open(archive) as tar:
        assert tar.extractfile(f'{name}/metadata.xml').read() == metadata
        assert tar.extractfile(f'{name}/{library_path}').read() == binary
        for output in manifest['outputs']:
            assert digest(tar.extractfile(f'{name}/{data_root}/data/{output["file"]}').read()) == output['sha256']
    result = dict(provenance, file=archive.name, bytes=archive.stat().st_size,
                  sha256=digest(archive.read_bytes()), url=url, metadata=xml.name,
                  translations=sum(n.endswith('.mo') for n in entries))
    (args.output / f'{name}.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, default=Path('build'))
    parser.add_argument('--kind', choices=['linux', 'flatpak', 'android', 'windows', 'macos'], required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--target-version', required=True)
    parser.add_argument('--arch', required=True)
    parser.add_argument('--api-version', default='1.18')
    parser.add_argument('--wx-version', choices=['3.2.9', '3.3.4'])
    parser.add_argument('--binary-source-revision', help='Retained CI binary revision when only repackaging data/locales')
    parser.add_argument('--output', type=Path)
    make_package(parser.parse_args())
