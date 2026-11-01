#!/usr/bin/env bash
set -euo pipefail
sdk=${1:?runtime version}
arch=${2:?architecture}
sudo apt-get update -qq
sudo apt-get install -y flatpak flatpak-builder libxml2-utils
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub "org.freedesktop.Sdk//$sdk" org.opencpn.OpenCPN//stable
python3 - "$sdk" <<'PY'
import json,sys,os
manifest = dict(id='org.opencpn.OpenCPN.Plugin.climatology', runtime='org.opencpn.OpenCPN',
               **{'runtime-version':'stable', 'sdk':'org.freedesktop.Sdk//'+sys.argv[1],
                  'build-extension':True,'separate-locales':False,'appstream-compose':False},
               modules=[dict(name='climatology', buildsystem='cmake', builddir=True,
                  **{'config-opts':['-DCMAKE_INSTALL_PREFIX=/app/extensions/climatology','-DOCPN_FLATPAK_BUILD=ON', '-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON','-DCMAKE_POLICY_VERSION_MINIMUM=3.5','-DSDK_VER='+sys.argv[1]],
                     'build-options':{'cflags':'-fPIC','cxxflags':'-fPIC -DFLATPAK'},
                     'sources':[{'type':'dir','path':os.getcwd(), 'skip':['.git','public-packages','.flatpak-builder','flatpak-build']}]} )])
open('flatpak-public.json','w').write(json.dumps(manifest,indent=2))
PY
flatpak-builder --user --force-clean --disable-rofiles-fuse flatpak-build flatpak-public.json
python3 ci/public-packages/package.py --binary flatpak-build/files/lib/opencpn/libclimatology_pi.so --build-dir flatpak-build/files --kind flatpak --target "flatpak-$arch" --target-version "$sdk" --arch "$arch"
for xml in public-packages/*.xml; do xmllint --noout --schema ci/public-packages/ocpn-plugin.xsd "$xml"; done
