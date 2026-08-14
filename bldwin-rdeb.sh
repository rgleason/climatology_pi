#!/bin/bash
echo "MSVC BUILD: Running script at $(date)"
echo "Run this script from the plugin root directory"

# --------------------------------------
# For Opencpn using MS Visual Studio 2022
# --------------------------------------
# Used for local builds and testing.
# Use Bash Prompt from the plugin root directory: "bash ./bldwin-rdeb.sh"
# Find any errors in build/BUILD-OUTPUT.txt
# Climatology_pi Windows bundles its own version of curl

# --- CONFIG ----------------------------------------------------------
Build_Type="RelWithDebInfo"   # Debug / Release / RelWithDebInfo
PLUGIN_NAME=$(basename "$(pwd)")
PLUGIN_ROOT="$(pwd)"
BUILD_DIR="$PLUGIN_ROOT/build"

OPENCPN_BUILD="/c/Users/fcgle/source/opencpn/build"
OPENCPN_RUNTIME="$OPENCPN_BUILD/$Build_Type/plugins"
OPENCPN_INSTALL="$OPENCPN_RUNTIME/$PLUGIN_NAME"

echo "Plugin dll/pdb + data installs into: $OPENCPN_INSTALL"
echo "Ready for testing with MSVC Win32 + $Build_Type"
echo "Plugin root:   $PLUGIN_ROOT"
echo "Build dir:     $BUILD_DIR"
echo "Runtime dir:   $OPENCPN_RUNTIME/$PLUGIN_NAME"
echo "Install dir:   $OPENCPN_INSTALL"
echo "Build type:    $Build_Type"

# --- CLEAN -----------------------------------------------------------
rm -rf "$BUILD_DIR" "$OPENCPN_INSTALL"
mkdir -p "$BUILD_DIR" "$OPENCPN_INSTALL"
cd "$BUILD_DIR"

# --- CONFIGURE -------------------------------------------------------
#cmake -G "Visual Studio 17 2022" \
#  -A Win32 \
#  -DwxWidgets_ROOT_DIR=/c/Users/fcgle/source/ocpn_wxWidgets/lib/mswud 
#  -DwxWidgets_ROOT_DIR=/c/Users/fcgle/source/ocpn_wxWidgets 
#  -CMAKE_BUILD_TYPE="$Build_Type" \
#  -CMAKE_INSTALL_PREFIX="$OPENCPN_BUILD" \
#   "$PLUGIN_ROOT"

#cmake -G "Visual Studio 17 2022" \
#  -cmake -T v143 -A Win32 -DOCPN_TARGET=MSVC .. \
#  -DwxWidgets_INCLUDE_DIRS=/c/Users/fcgle/source/ocpn_wxWidgets/lib/vc_dll/mswud \
#  -DwxWidgets_LIBRARIES=/c/Users/fcgle/source/ocpn_wxWidgets/lib/vc_dll \
#  -DMAKE_BUILD_TYPE="$Build_Type" \
#  -DMAKE_INSTALL_PREFIX="$OPENCPN_BUILD" \
#  "$PLUGIN_ROOT"

cmake -G "Visual Studio 17 2022" \
  -A Win32 \
  -DwxWidgets_ROOT_DIR=/c/Users/fcgle/source/ocpn_wxWidgets \
  -DwxWidgets_INCLUDE_DIRS=/c/Users/fcgle/source/ocpn_wxWidgets/include \
  -DwxWidgets_LIB_DIR=/c/Users/fcgle/source/ocpn_wxWidgets/lib/vc_dll \
  -DwxWidgets_LIBRARIES="wxmsw32u_core;wxmsw32u_base;wxmsw32u_gl;wxmsw32u_html;wxmsw32u_adv;wxmsw32u_aui;wxmsw32u_xml;wxmsw32u_net" \
  -DCMAKE_BUILD_TYPE="$Build_Type" \
  -DCMAKE_INSTALL_PREFIX="$OPENCPN_BUILD" \
  "$PLUGIN_ROOT"

# ORIGINAL
#cmake -G "Visual Studio 17 2022" \
#  -A Win32 \
#  -DwxWidgets_ROOT_DIR=/c/Users/fcgle/source/ocpn_wxWidgets \
#  -DwxWidgets_CONFIG=mswud \
#  -DCMAKE_BUILD_TYPE="$Build_Type" \
#  -DCMAKE_INSTALL_PREFIX="$OPENCPN_BUILD" \
#  "$PLUGIN_ROOT"


# wxWidgets settings 
#set "wxDIR=C:\Users\fcgle\source\opencpn\..\ocpn_wxWidgets" 
#set "wxWIN=C:\Users\fcgle\source\opencpn\..\ocpn_wxWidgets" 
#set "wxWidgets_ROOT_DIR=C:\Users\fcgle\source\opencpn\..\ocpn_wxWidgets" 
#set "wxWidgets_LIB_DIR=C:\Users\fcgle\source\opencpn\..\ocpn_wxWidgets\lib\vc_dll" 
#set "VCver=17" 
#set "VCstr=Visual Studio 17" 

# build the plugin with cmake
# Remove build dir and make a new one
#rm -rf build
#mkdir build
#cd build
#cmake -T v143 -A Win32 -DOCPN_TARGET=MSVC ..
#cmake --build . --target package --config relwithdebinfo >output.txt
	
# Bash script completes tarball prep adding metadata into it.



 
# --- BUILD + INSTALL -------------------------------------------------
cmake --build . --config "$Build_Type" --target package >"$PLUGIN_ROOT"/OUTPUT.txt
cmake --build . --config "$Build_Type" --target install

# --- COPY REQUIRED RUNTIME FILES ------------------------------------
echo "Copying DLL, PDB, manifest.json, cacert.pem to runtime..."
cp -uv "$BUILD_DIR/$Build_Type/${PLUGIN_NAME}.dll" "$OPENCPN_INSTALL/"
cp -uv "$BUILD_DIR/$Build_Type/${PLUGIN_NAME}.pdb" "$OPENCPN_INSTALL/"
cp -uv "$PLUGIN_ROOT/manifest.json" "$OPENCPN_INSTALL/"
cp -uv "$PLUGIN_ROOT/cacert.pem" "$OPENCPN_INSTALL/"
cp -uvr "$PLUGIN_ROOT/data" "$OPENCPN_INSTALL/"
cp -uvr "$PLUGIN_ROOT/usericons" "$OPENCPN_INSTALL/"

# --- PACKAGE + LIST --------------------------------------------------
bash "$BUILD_DIR/cloudsmith-upload.sh"
ls -l *.gz *.xml metadata.xml 2>/dev/null
ls -l "$OPENCPN_RUNTIME/"*_pi.*


# Copy DLL + PDB
# cp -uv ./RelWithDebInfo/*_pi.dll "$PLUGIN_RUNTIME_ROOT/"
# cp -uv ./RelWithDebInfo/*_pi.pdb "$PLUGIN_RUNTIME_ROOT/"

# Find ${bold}"build/Build-Output.txt"${normal} file if the build is not successful.

# Other examples below.
# cp -rv ./SourceFolder ./DestFolder
# cp -r ./dist/* ./out
#    -r - Copy all files and folders inside a directory
#    -i - Ask before replacing files
#    -u - Copy only if the source is newer
#    -v - Verbose mode, show files being copied

# End of script

