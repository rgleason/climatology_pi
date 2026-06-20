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

echo "RELWITHDEBINFO: Running script at $(date)"

# Plugin and build directories
PLUGIN_ROOT="/c/Users/fcgle/source/climatology_pi"
BUILD_DIR="$PLUGIN_ROOT/build"

# OpenCPN runtime directory (where plugins actually load)
OCPN_RUNTIME="C:/Users/fcgle/source/opencpn/build/RelWithDebInfo"
OCPN_RUNTIME_POSIX=$(cygpath "$OCPN_RUNTIME")

echo "OpenCPN runtime: $OCPN_RUNTIME_POSIX"
echo "Plugin root:     $PLUGIN_ROOT"
echo "Build dir:       $BUILD_DIR"

# Clean build directory
rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure plugin build
cmake -G "Visual Studio 17 2022" \
  -A Win32 \
  -DwxWidgets_ROOT_DIR=/c/Users/fcgle/source/ocpn_wxWidgets \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_INSTALL_PREFIX="$OCPN_RUNTIME" \
  ..

# Build plugin package
cmake --build . --config RelWithDebInfo --target package > Build-Output.txt

# Install plugin (DLL, metadata, manifest.json, UserIcons, cacert.pem, data/, etc.)
cmake --build . --config RelWithDebInfo --target install

# Inject metadata into tarball
bash "$BUILD_DIR/cloudsmith-upload.sh"

echo "Plugin install complete."

# Show installed plugin files
echo ""
echo "Installed plugin files:"
ls -l "$OCPN_RUNTIME_POSIX/plugins/climatology_pi"

echo ""
echo "Build finished."

# Find ${bold}"build/Build-Output.txt"${normal} file if the build is not successful.

# Other examples below.
# cp -rv ./SourceFolder ./DestFolder
# cp -r ./dist/* ./out
#    -r - Copy all files and folders inside a directory
#    -i - Ask before replacing files
#    -u - Copy only if the source is newer
#    -v - Verbose mode, show files being copied


# Copy DLL + PDB
#cp -uv ./RelWithDebInfo/*_pi.dll "$PLUGIN_RUNTIME_ROOT/"
#cp -uv ./RelWithDebInfo/*_pi.pdb "$PLUGIN_RUNTIME_ROOT/"

# Copy metadata.xml
# cp -uv ./RelWithDebInfo/metadata.xml "$PLUGIN_RUNTIME_ROOT/"

# Copy installed assets
# cp -ruv "$PLUGIN_INSTALL_DIR"/data "$PLUGIN_RUNTIME_DIR/"
# cp -ruv "$PLUGIN_INSTALL_DIR"/UserIcons "$PLUGIN_RUNTIME_DIR/"

# Copy manifest.json last
# cp -uv "$PLUGIN_ROOT/manifest.json" "$PLUGIN_RUNTIME_DIR/"

# Copy CA certificate bundle for curl
# cp -uv "$PLUGIN_ROOT/cacert.pem" "$PLUGIN_RUNTIME_DIR/"

# Copy libcurl
# cp -uv "$PLUGIN_ROOT/win_curl/output32/bin/libcurl.dll" "$PLUGIN_RUNTIME_DIR/"