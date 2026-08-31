#!/usr/bin/env bash
set -xe

#
# Clean, Windows‑safe Cloudsmith upload script for MSVC builds
#   • Uses PROJECT_VERSION from pkg_version.sh
#   • Uses PACKAGE_NAME, PKG_TARGET, PKG_TARGET_VERSION from pkg_version.sh
#   • Eliminates hardcoded versions
#   • Ensures metadata.xml matches the tarball name
#

############################################
# Resolve plugin root and build directory
############################################

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_ROOT="$SCRIPT_DIR"
BUILD_DIR="$PLUGIN_ROOT/build"

echo "SCRIPT_DIR:  $SCRIPT_DIR"
echo "PLUGIN_ROOT: $PLUGIN_ROOT"
echo "BUILD_DIR:   $BUILD_DIR"

############################################
# Cloudsmith repo definitions
############################################

PROD_REPO=${CLOUDSMITH_PROD_REPO:-'opencpn/climatology-prod'}
BETA_REPO=${CLOUDSMITH_BETA_REPO:-'opencpn/climatology-beta'}
ALPHA_REPO=${CLOUDSMITH_ALPHA_REPO:-'opencpn/climatology-alpha'}

############################################
# Determine build environment
############################################

LOCAL_BUILD=false

if [ "$CIRCLECI" ]; then
    BUILD_ID=${CIRCLE_BUILD_NUM:-1}
    BUILD_BRANCH=$CIRCLE_BRANCH
    BUILD_TAG=$CIRCLE_TAG
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'deb'}

elif [ "$TRAVIS" ]; then
    BUILD_ID=${TRAVIS_BUILD_NUM:-1}
    BUILD_BRANCH=$TRAVIS_BRANCH
    BUILD_TAG=$TRAVIS_TAG
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'deb'}

elif [ "$APPVEYOR" ]; then
    BUILD_ID=${APPVEYOR_BUILD_NUMBER:-1}
    BUILD_BRANCH=$APPVEYOR_REPO_BRANCH
    BUILD_TAG=$APPVEYOR_REPO_TAG_NAME
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'exe'}

else
    # LOCAL WINDOWS/MSVC BUILD
    BUILD_ID=1
    BUILD_BRANCH=""
    BUILD_TAG=""
    PKG_EXT=${CLOUDSMITH_PKG_EXT:-'exe'}
    LOCAL_BUILD=true
fi

############################################
# API key check (skip for local builds)
############################################

set +x
if [ -z "$CLOUDSMITH_API_KEY" ] && [ "$LOCAL_BUILD" = "false" ]; then
    echo "Missing CLOUDSMITH_API_KEY — skipping upload"
    exit 0
fi
set -x

############################################
# Git info
############################################

commit=$(git -C "$PLUGIN_ROOT" rev-parse --short=7 HEAD || echo "unknown")
tag=$(git -C "$PLUGIN_ROOT" tag --contains HEAD || echo "")

############################################
# Load version info from CMake
############################################

source "$BUILD_DIR/pkg_version.sh"

# Explicitly set values not provided by pkg_version.sh
PACKAGE_NAME="climatology_pi"
ARCH="x86"

# PROJECT_VERSION="1.6.48.0"
# PKG_TARGET="msvc"
# PKG_TARGET_VERSION="10.0.26200"

############################################
# Locate artifacts
############################################

cd "$BUILD_DIR"

# xml=$(ls *.xml 2>/dev/null || echo "")
xml=$(ls ${PACKAGE_NAME}-*.xml 2>/dev/null || echo "")
tarball=$(ls *.tar.gz 2>/dev/null || echo "")
pkg=$(ls *.${PKG_EXT} 2>/dev/null || echo "")

if [ -z "$xml" ]; then
    echo "ERROR: No XML file found in $BUILD_DIR"
    exit 1
fi

if [ -z "$tarball" ]; then
    echo "ERROR: No tar.gz file found in $BUILD_DIR"
    exit 1
fi

tarball_basename=${tarball##*/}


############################################
# Build correct tarball name
############################################

if [ -n "$OCPN_TARGET" ]; then
	tarball_name="${PACKAGE_NAME}-${VERSION}-msvc-${ARCH}-wx32-${PKG_TARGET_VERSION}-MSVC"

else
	tarball_name="${PACKAGE_NAME}-${VERSION}-msvc-${ARCH}-wx32-${PKG_TARGET_VERSION}-MSVC"

fi


############################################
# Determine repo and version
############################################

BUILD_BRANCH_LOWER=$(echo "$BUILD_BRANCH" | tr 'A-Z' 'a-z')

if [ "$BUILD_BRANCH_LOWER" = "master" ]; then
    if [ -n "$BUILD_TAG" ]; then
        VERSION="$BUILD_TAG"
        REPO="$PROD_REPO"
    else
        VERSION="${PROJECT_VERSION}+${BUILD_ID}.${commit}"
        REPO="$BETA_REPO"
    fi
else
    if [ -n "$BUILD_TAG" ]; then
        VERSION="$BUILD_TAG"
        REPO="$BETA_REPO"
    else
        VERSION="${PROJECT_VERSION}+${BUILD_ID}.${commit}"
        REPO="$ALPHA_REPO"
    fi
fi

echo "VERSION: $VERSION"
echo "REPO:    $REPO"
echo "TARBALL: $tarball_name"

############################################
# Windows-safe XML substitution
############################################

echo "Substituting XML metadata..."

while read -r line; do
    line=${line//--pkg_repo--/$REPO}
    line=${line//--name--/$tarball_name}
    line=${line//--version--/$VERSION}
    line=${line//--filename--/$tarball_basename}
    echo "$line"
done < "$xml" > xml.tmp

mv xml.tmp "$xml"

############################################
# Repack tarball with metadata.xml
############################################

echo "Repacking tarball..."

gunzip -f "$tarball"
tarball_tar="${tarball%.gz}"

cp "$xml" metadata.xml
tar -rf "$tarball_tar" metadata.xml
gzip -f "$tarball_tar"

############################################
# Upload (skip for local builds)
############################################

if [ "$LOCAL_BUILD" = true ]; then
    echo "Local Windows build — skipping Cloudsmith upload"
    exit 0
fi

cloudsmith push raw --republish --no-wait-for-sync \
    --name "${tarball_name}" \
    --version "${VERSION}" \
    --summary "Climatology plugin tarball" \
    "$REPO" "$tarball"

if [ -n "$pkg" ]; then
    cloudsmith push raw --republish --no-wait-for-sync \
        --name "climatology-package-${VERSION}.${PKG_EXT}" \
        --version "${VERSION}" \
        --summary "Climatology installer package" \
        "$REPO" "$pkg"
fi

echo "Cloudsmith upload complete."
