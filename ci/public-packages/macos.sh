#!/usr/bin/env bash
set -euo pipefail
export HOMEBREW_NO_AUTO_UPDATE=1
brew list gettext >/dev/null 2>&1 || brew install gettext
export PATH="$(brew --prefix gettext)/bin:$PATH"
curl --fail --location --retry 3 https://dl.cloudsmith.io/public/nohal/opencpn-plugins/raw/files/macos_deps_universal.tar.xz -o /tmp/climatology-macos-deps.tar.xz
shasum -a 256 /tmp/climatology-macos-deps.tar.xz
sudo tar -C /usr/local -xJf /tmp/climatology-macos-deps.tar.xz
export PATH="/usr/local/bin:$PATH"
export WX_CONFIG=/usr/local/bin/wx-config
export WX_VER=32 OCPN_TARGET=macos
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' -DwxWidgets_CONFIG_EXECUTABLE=/usr/local/bin/wx-config -DOPENSSL_ROOT_DIR=/usr/local
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
(cd build && bash ../cmake/fix-macos-libs.sh)
lipo build/libclimatology_pi.dylib -verify_arch arm64 x86_64
python3 ci/public-packages/package.py --binary build/libclimatology_pi.dylib --kind macos --target darwin-wx32 --target-version 10 --arch 'arm64;x86_64'
for xml in public-packages/*.xml; do xmllint --noout --schema ci/public-packages/ocpn-plugin.xsd "$xml"; done
