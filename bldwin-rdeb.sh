#!/bin/bash

# FE2 Testplugin
# RelWithDebIinfo VERSION
# Use "./build-win.sh" to run cmake.
# Adjust this command for your setup and Plugin.
# Requires wxWidgets setup
# - /home/fcgle/source/ocpn-wxWidgets
# - /home/fcgle/source/ where all the plugins and OpenCPN repos are kept.
# --------------------------------------
# For Opencpn using MS Visual Studio 2022
# --------------------------------------
# Used for local builds and testing.
# Create an empty "[plugin]/build" directory
# Use Bash Prompt from the [plugin] root directory: "bash ./bldwin-rdeb.sh"
# Find any errors in the build/output.txt file
# Then use bash prompt to run cloudsmith-upload.sh command: "bash ./bldwin-rdeb.sh"
# Which adds the metadata file to the tarball gz file.
# Set local environment to find and use wxWidgets

echo "RELWITHDEBINFO: Running THIS script at $(date)"

# Determine plugin root
PLUGIN_ROOT="/c/Users/fcgle/source/climatology_pi"
BUILD_DIR="$PLUGIN_ROOT/build"

# Windows-style path for OpenCPN runtime
OPENCPN_DIR="C:\Users\fcgle\source\opencpn\build\OpenCPN"
# OPENCPN_DIR="C:\Users\fcgle\source\opencpn\build\RelWithDebInfo"
OPENCPN_POSIX=$(cygpath "$OPENCPN_DIR")

echo "OpenCPN POSIX path: $OPENCPN_POSIX"
echo "Plugin root: $PLUGIN_ROOT"
echo "Build dir:   $BUILD_DIR"

# Clean build directory
rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# CMake configure
# cmake -G "Visual Studio 17 2022" \
#  -A Win32 \
#  -DwxWidgets_ROOT_DIR=/c/Users/fcgle/source/ocpn_wxWidgets \
#  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
# -DCMAKE_INSTALL_PREFIX="C:/Users/fcgle/source/opencpn/build/RelWithDebInfo"\
#  ..
  
cmake -G "Visual Studio 17 2022" \
  -A Win32 \
  -DwxWidgets_ROOT_DIR=/c/Users/fcgle/source/ocpn_wxWidgets \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="C:/Users/fcgle/source/opencpn/build/RelWithDebInfo"\
  ..
# Curl cmake statements not needed due to set(curl in CMakeLists.txt
  
# Build plugin package
cmake --build . --config RelWithDebInfo --target package > Build-Output.txt

# Install plugin assets
cmake --build . --config RelWithDebInfo --target install

# Add metadata + tarball
bash "$BUILD_DIR/cloudsmith-upload.sh"

# --- PATH DEFINITIONS (define once, use everywhere) ---

# Where MSVC/OpenCPN loads plugins at runtime
PLUGIN_RUNTIME_ROOT="/c/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins"
PLUGIN_RUNTIME_DIR="$PLUGIN_RUNTIME_ROOT/climatology_pi"

# Where CMake installs the plugin
PLUGIN_INSTALL_DIR="/c/Users/fcgle/source/opencpn/build/OpenCPN/plugins/climatology_pi"

# --- CLEAN OLD RUNTIME PLUGIN ---

echo "Cleaning old MSVC runtime plugin..."
rm -f "$PLUGIN_RUNTIME_ROOT/climatology_pi.dll"
rm -f "$PLUGIN_RUNTIME_ROOT/climatology_pi.pdb"
rm -f "$PLUGIN_RUNTIME_ROOT/metadata.xml"
rm -rf "$PLUGIN_RUNTIME_DIR"
mkdir -p "$PLUGIN_RUNTIME_DIR"
echo "Old MSVC plugin removed."

# --- MIRROR NEW PLUGIN INTO RUNTIME ---
#  None of this is needed now! We changed  to NOT TRUE
#  -DCMAKE_INSTALL_PREFIX="C:/Users/fcgle/source/opencpn/build/RelWithDebInfo"\
# NOT TRUE

echo "Mirroring plugin into MSVC runtime..."

# Copy DLL + PDB
cp -uv ./RelWithDebInfo/*_pi.dll "$PLUGIN_RUNTIME_ROOT/"
cp -uv ./RelWithDebInfo/*_pi.pdb "$PLUGIN_RUNTIME_ROOT/"

# Copy metadata.xml
cp -uv ./RelWithDebInfo/metadata.xml "$PLUGIN_RUNTIME_ROOT/"

# Copy installed assets
cp -ruv "$PLUGIN_INSTALL_DIR"/data "$PLUGIN_RUNTIME_DIR/"
cp -ruv "$PLUGIN_INSTALL_DIR"/UserIcons "$PLUGIN_RUNTIME_DIR/"

# Copy manifest.json last
cp -uv "$PLUGIN_ROOT/manifest.json" "$PLUGIN_RUNTIME_DIR/"

# Copy CA certificate bundle for curl
cp -uv "$PLUGIN_ROOT/cacert.pem" "$PLUGIN_RUNTIME_DIR/"

# Copy libcurl
cp -uv "$PLUGIN_ROOT/win_curl/output32/bin/libcurl.dll" "$PLUGIN_RUNTIME_DIR/"

echo "MSVC plugin install complete."

# --- SHOW RESULTS ---
ls -l *.gz *.xml metadata.xml 2>/dev/null
echo ""
echo "List plugin files now in OpenCPN plugin directory:"
ls -l "$OPENCPN_POSIX/plugins/"*_pi.*


# Find ${bold}"build/Build-Output.txt"${normal} file if the build is not successful.

# Other examples below.
# cp -rv ./SourceFolder ./DestFolder
# cp -r ./dist/* ./out
#    -r - Copy all files and folders inside a directory
#    -i - Ask before replacing files
#    -u - Copy only if the source is newer
#    -v - Verbose mode, show files being copied

