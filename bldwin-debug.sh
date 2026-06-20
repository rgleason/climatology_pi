#!/usr/bin/env bash
# Relaunch under Git Bash if not already in Bash
if [ -z "${BASH_VERSION:-}" ]; then
    echo "Re-launching under Git Bash..."
    exec bash "$0" "$@"
fi
set -euo pipefail

# ------------------------------------------------------------
#  Color definitions
# ------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

section() { echo -e "${BOLD}${CYAN}=== $1 ===${RESET}" }
ok() { echo -e "${GREEN}$1${RESET}" }
warn() { echo -e "${YELLOW}$1${RESET}" }
err() { echo -e "${RED}$1${RESET}" }

# ============================================================
#  Climatology Plugin — Windows Build Script (Bash Version)
#  Works in Git Bash / MSYS2 / MINGW64 / WSL
#  Builds Debug + RelWithDebInfo using MSVC
#  Auto‑copies plugin DLL/PDB into OpenCPN dev tree
# ============================================================

# ------------------------------------------------------------
#  Resolve script directory
# ------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ------------------------------------------------------------
#  Automatic OpenCPN build tree detection
# ------------------------------------------------------------
detect_ocpn_tree() {
    local start="$SCRIPT_DIR"
    local dir="$start"

    while [[ "$dir" != "/" ]]; do
        # Look for a CMake build directory
        if [[ -d "$dir/build" ]]; then
            # Check for MSVC Debug plugin folder
            if [[ -d "$dir/build/Debug/plugins" ]]; then
                echo "$dir/build/Debug/plugins"
                return 0
            fi

            # Check for MSVC RelWithDebInfo plugin folder
            if [[ -d "$dir/build/RelWithDebInfo/plugins" ]]; then
                echo "$dir/build/RelWithDebInfo/plugins"
                return 0
            fi
        fi

        # Move up one directory
        dir="$(dirname "$dir")"
    done

    # Fallback if nothing found
    echo ""
    return 1
}

# ------------------------------------------------------------
#  Detect Windows username (Git Bash / MSYS2 / WSL / Linux)
# ------------------------------------------------------------
detect_windows_user() {
    if [[ -n "${USERNAME:-}" ]]; then
        echo "$USERNAME"
    elif [[ -n "${USER:-}" ]]; then
        echo "$USER"
    else
        echo "unknown"
    fi
}

WINUSER="$(detect_windows_user)"

# User-specific paths
# ------------------------------------------------------------
#  wxWidgets root (CONFIG-mode)
# ------------------------------------------------------------
WX_ROOT="/c/Users/$WINUSER/source/wxWidgets-3.2.1"

# Example: export wxWidgets_ROOT_DIR="/c/Users/fcgle/source/wxWidgets"
export wxWidgets_ROOT_DIR="$WX_ROOT"

if [[ ! -d "$wxWidgets_ROOT_DIR" ]]; then
    err "wxWidgets directory not found:"
    err "  $wxWidgets_ROOT_DIR"
    exit 1
fi


# ------------------------------------------------------------
#  OpenCPN dev tree
# ------------------------------------------------------------
# Example: OCPN_ROOT="/c/Users/$WINUSER/source/opencpn"

OCPN_DEV="$(detect_ocpn_tree)"

if [[ -z "$OCPN_DEV" ]]; then
	warn "Could not automatically detect OpenCPN build tree."
	warn "Plugin DLL/PDB will NOT be auto-copied."
else
	section "Detected OpenCPN plugin directory"
	echo -e "  ${BOLD}$OCPN_DEV${RESET}"
fi

# ------------------------------------------------------------
#  Plugin build directory
# ------------------------------------------------------------
PLUGIN_BUILD="${SCRIPT_DIR}/build"

section "Cleaning build directory"
rm -rf "$PLUGIN_BUILD"
mkdir -p "$PLUGIN_BUILD"
cd "$PLUGIN_BUILD"

# ------------------------------------------------------------
#  CONFIGURE PLUGIN
# ------------------------------------------------------------
echo
echo "Using wxWidgets from: $wxWidgets_ROOT_DIR"
section "Configuring plugin"


cmake -T v143 -A Win32 -DOCPN_TARGET=MSVC \
  -DwxWidgets_ROOT_DIR="$wxWidgets_ROOT_DIR" \
  .. > cmake_configure_log.txt 2>&1

ok "CMake configure OK."




# ------------------------------------------------------------
#  BUILD DEBUG and RELWITHDEBINFO in PARALLEL
# ------------------------------------------------------------
echo
section "Building Debug"

cmake --build . --target package --config Debug --parallel \
  > buildlog-debug.txt 2>&1

ok "Debug build OK."

cmake --install . --config Debug

# ------------------------------------------------------------
#  BUILD RELWITHDEBINFO
# ------------------------------------------------------------
echo
section "Building RelWithDebInfo"

cmake --build . --target package --config RelWithDebInfo \
  > buildlog-rdeb.txt 2>&1

ok "RelWithDebInfo build OK."

cmake --install . --config RelWithDebInfo

# ------------------------------------------------------------
#  AUTO‑COPY DLL + PDB INTO OPENCPN DEV TREE
# ------------------------------------------------------------
echo
section "Copying plugin DLL/PDB into OpenCPN dev tree"

# copy ..\build\relwithdebinfo\weather_routing_pi.dll to  C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins
# copy ..\build\relwithdebinfo\weather_routing_pi.pdb to  C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins

cp -uv ./RelWithDebInfo/*_pi.dll C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins
cp -uv ./RelWithDebInfo/*_pi.pdb C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins


# for cfg in Debug RelWithDebInfo; do
#    if compgen -G "$cfg/*.dll" > /dev/null; then
#        cp -v "$cfg"/*.dll "$OCPN_DEV"
#    fi
#    if compgen -G "$cfg/*.pdb" > /dev/null; then
#        cp -v "$cfg"/*.pdb "$OCPN_DEV"
#    fi
#done

echo "Copy complete."

# Return to plugin root


cd "$SCRIPT_DIR"

# ------------------------------------------------------------
#  RUN CLOUDSMITH METADATA INJECTION
# ------------------------------------------------------------
echo
section "Running cloudsmith-upload.sh"
if [[ -f "./build/cloudsmith-upload.sh" ]]; then
    (
        cd ./build
        bash ./cloudsmith-upload.sh
    )
else
    warn "cloudsmith-upload.sh not found."
    warn "Expected: ./build/cloudsmith-upload.sh"
fi
section "Build complete"
ok "Debug + RelWithDebInfo builds are ready."
echo -e "Plugin deployed to: ${BOLD}$OCPN_DEV${RESET}"
echo -e "wxWidgets used from: ${BOLD}$wxWidgets_ROOT_DIR${RESET}"
# NOTES
# The tarball is always the RelWithDebInfo package.
# OpenCPN plugin packaging rules:
# Debug build → used only for local debugging
# RelWithDebInfo build → used for packaging
# CPack → always packages the RelWithDebInfo artifacts
# So this file:
#     weather_routing_pi-1.15.45.7-msvc-x86-wx32-10.0.26200-MSVC.tar.gz
# contains:
#    The RelWithDebInfo DLL
#    The metadata (after  ./cloudsmith-upload.sh is run)
#    The XML manifest
#    The plugin resources
#    This is the file you would upload to Cloudsmith