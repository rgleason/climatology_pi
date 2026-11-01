"""Verify a retained, byte-identical package in a fresh Windows host VM."""
import hashlib,json,subprocess,sys,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[2]
archive=next(Path('retained-package/public-packages').glob('*.tar.gz')).resolve()
arch=sys.argv[1]
work=root/'.windows-runtime-host'/arch
work.mkdir(parents=True,exist_ok=True)
if arch=='x64':
 pin=json.loads((root/'ci/public-packages/windows64-sdk.json').read_text())
 url=pin['host_url'];expected=pin['host_sha256']
else:
 url='https://github.com/OpenCPN/OpenCPN/releases/download/Release_5.14.2/opencpn_5.14.2-0%2B4828.37fcbab_setup.exe'
 expected='ef43f23018c1577b05b6e7333bcf89becc8f3cdad2051c958a5fc778ca9de25d'
installer=work/'host.exe'
urllib.request.urlretrieve(url,installer)
assert hashlib.sha256(installer.read_bytes()).hexdigest()==expected
host=work/'host'
subprocess.run(['7z','x','-y','-o'+str(host),str(installer)],check=True)
subprocess.run([sys.executable,str(root/'ci/public-packages/windows-host.py'),str(archive),str(host)],check=True)
