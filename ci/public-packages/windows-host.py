"""Import the complete package, inspect its GUI version and require normal shutdown."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[2]
archive = Path(sys.argv[1]).resolve()
host = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else root / '.windows86-host/host'
output = root / 'artifacts/windows-version'
output.mkdir(parents=True, exist_ok=True)
assert sys.platform == 'win32' and os.environ.get('GITHUB_ACTIONS') == 'true', 'Disposable Windows CI only'
ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
if len(sys.argv) == 2:
    installer = host.parent / 'opencpn-5.14.2-x86.exe'
    url = 'https://github.com/OpenCPN/OpenCPN/releases/download/Release_5.14.2/opencpn_5.14.2-0%2B4828.37fcbab_setup.exe'
    expected = 'ef43f23018c1577b05b6e7333bcf89becc8f3cdad2051c958a5fc778ca9de25d'
    installer.parent.mkdir(parents=True, exist_ok=True)
    urllib.request.urlretrieve(url, installer)
    assert hashlib.sha256(installer.read_bytes()).hexdigest() == expected
    subprocess.run(['7z', 'x', '-y', '-o' + str(host), str(installer)], check=True)
environment = dict(os.environ, LOCALAPPDATA=str(output / 'local-appdata'),
                   APPDATA=str(output / 'roaming-appdata'))
environment['PATH'] = str(host) + os.pathsep + environment['PATH']
profile = Path(os.environ['PROGRAMDATA']) / 'opencpn'
config = profile / 'opencpn.ini'
assert not config.exists(), 'Refusing to replace an existing OpenCPN profile'
binary = (host / 'opencpn.exe').read_bytes()
versions = re.findall(rb'5\.\d+\.\d+(?:-[A-Za-z0-9._]+)?(?:\+[A-Za-z0-9._]+)?\x00', binary)
dates = re.findall(rb'20\d{2}-\d{2}-\d{2}\x00', binary)
assert versions and dates
version = max(versions, key=len)[:-1].decode()
date = max(dates)[:-1].decode()
profile.mkdir(parents=True, exist_ok=True)
text = (f'[Settings]\nConfigVersionString=Version {version} Build {date}\n'
        'NavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\nShowMenuBar=1\nDataConnections=\n'
        '[PlugIns/climatology_pi.dll]\nbEnabled=1\n')
for bundled in (host / 'plugins').glob('*.dll'):
    text += f'[PlugIns/{bundled.name}]\nbEnabled=0\n'
config.write_text(text, encoding='utf-8')
with tarfile.open(archive) as package:
    metadata = next(m for m in package if m.name.endswith('/metadata.xml'))
    plugin_version = ET.fromstring(package.extractfile(metadata).read()).findtext('version')
    displayed_version = plugin_version.removesuffix('.0')
receipt = dict(host_version=version, plugin_version=plugin_version,
               package_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(), cycles=[])
for cycle in range(2):
    for name, args in [('abi', ['print-abi']), ('import', ['import-plugin', str(archive)])]:
        command = subprocess.run([str(host / 'opencpn-cmd.exe'), *args], cwd=host,
                                 env=environment, capture_output=True, text=True, timeout=90)
        (output / f'{name}-{cycle}.log').write_text(command.stdout + command.stderr, encoding='utf-8')
        assert command.returncode == 0, (name, command.returncode, command.stdout, command.stderr)
    library = Path(environment['LOCALAPPDATA']) / 'opencpn/plugins/climatology_pi.dll'
    assert library.is_file(), 'Plugin Manager did not install the DLL'
    with tarfile.open(archive) as package:
        for item in package:
            if item.isfile() and '/plugins/' in item.name:
                suffix = item.name.split('/plugins/', 1)[1]
                installed = library.parent / suffix
                assert installed.is_file() and installed.read_bytes() == package.extractfile(item).read(), suffix
    log = profile / 'opencpn.log'
    if log.exists():
        log.unlink()
    if cycle == 0:
        subprocess.run(['pwsh', '-File', str(root / 'ci/public-packages/windows-version.ps1'),
                        '-HostRoot', str(host), '-Output', str(output),
                        '-ExpectedVersion', displayed_version], env=environment, check=True, timeout=150)
        code = json.loads((output / 'version-result.json').read_text(encoding='utf-8-sig'))['exit_code']
    else:
        with (output / f'stdout-{cycle}.log').open('w') as stdout:
            app = subprocess.Popen([str(host / 'opencpn.exe'), '--no_opengl'], cwd=host,
                                   env=environment, stdout=stdout, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline and app.poll() is None:
                    if log.exists() and 'Finalize Canvases' in log.read_text(errors='replace'):
                        break
                    time.sleep(0.2)
                else:
                    raise RuntimeError('OpenCPN did not complete startup')
                subprocess.run([str(host / 'opencpn.exe'), '--remote', '--quit'],
                               cwd=host, env=environment, check=True, timeout=30)
                code = app.wait(timeout=30)
            finally:
                if app.poll() is None:
                    app.kill()
                    app.wait()
    assert code == 0, ('Normal shutdown failed', code)
    assert log.is_file(), 'OpenCPN did not produce a runtime log'
    shutil.copy2(log, output / f'host-{cycle}.log')
    text = log.read_text(errors='replace')
    assert re.search(r'PluginLoader: (?:Loading|Initializing) PlugIn:.*climatology_pi\.dll', text), 'Climatology was not initialized'
    assert not re.search(r'(?:failed to load|cannot load|undefined symbol).*climatology', text, re.I)
    receipt['cycles'].append(dict(cycle=cycle, exit_code=code, plugin_initialized=True))
    (output / 'runtime-result.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(f'Stock OpenCPN imported the exact complete dataset package and displayed Climatology {displayed_version}; two normal exits')
