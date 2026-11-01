#!/usr/bin/env bash
set -euo pipefail
arch=${1:?arm64 or armhf}
common_commit=0bb3fe2ac79c25ed1a2d7943732203c2a4010035
if [[ ! -d android-common ]]; then
  mkdir -p android-common
  curl --fail --location --retry 3 "https://github.com/bdbcat/OCPNAndroidCommon/archive/$common_commit.tar.gz" | tar -xz --strip-components=1 -C android-common
fi
export NDK_HOME=${NDK_HOME:-${ANDROID_HOME:?}/ndk/26.1.10909125}
export OCPN_TARGET=android-$arch
cmake -S . -B "build-android-$arch" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  "-DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/android-${arch/arm64/aarch64}-toolchain.cmake" \
  "-D_wx_selected_config=androideabi-qt-$arch" -DOCPN_Android_Common=android-common
cmake --build "build-android-$arch" --parallel 3
readelf -h "build-android-$arch/libclimatology_pi.so"
readelf -d "build-android-$arch/libclimatology_pi.so" | tee "build-android-$arch/dependencies.txt"
grep -q 'SONAME.*libclimatology_pi.so' "build-android-$arch/dependencies.txt"
grep -q 'NEEDED.*libgorp.so' "build-android-$arch/dependencies.txt"
python3 ci/public-packages/package.py --binary "build-android-$arch/libclimatology_pi.so" --build-dir "build-android-$arch" --kind android --target "android-$arch" --target-version 16 --arch "$arch"
for xml in public-packages/*.xml; do xmllint --noout --schema ci/public-packages/ocpn-plugin.xsd "$xml"; done
