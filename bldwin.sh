#!/bin/bash
set -x  # Enable command tracing
	  
# ------------------------------------------------------------
# MSVC x86 Native Tools environment
# ------------------------------------------------------------
# We MUST invoke MSVC tools through cmd.exe so Git Bash does not
# rewrite paths or corrupt the environment.
# This loads cl.exe, link.exe, nmake, and the correct CRT.
# ------------------------------------------------------------

MSVC_ENV='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'

# FE2 Testplugin
# Use ${bold}"./build-win.sh c"${normal} to run cmake.
# Use "./build.sh c pi" to run cmake and copy a DLL for testing.
# Adjust this command for your setup and Plugin.
# Requires wxWidgets setup
# - /home/fcgle/source/wxWidgets-3.1.2
# - /home/fcgle/source/wxWidgets-3.2.2
# - /home/fcgle/source/ is where all the plugins and OpenCPN repos are kept.
# Visual Studio 15 2017 installed
# Visual Studio 17 2022 installed
# --------------------------------------
# For Opencpn 5.8 and wxWidgets-3.2.2 using Visual Studio 15 2017
# --------------------------------------
# Used for local builds and testing.
# Create an empty "[plugin]/build" directory
# Use a terminal from the [plugin] root directory: "./bld.sh"
# Find the errors in the build/output.txt file
# Then use bash prompt to run cloudsmith-upload.sh command: "bash ./bld.sh"
# This adds the metadata file to the tarball gz file.
# Set local environment to find and use wxWidgets-3.2.2

wxDIR=$WXWIN
wxWidgets_ROOT_DIR=$WXWIN
wxWidgets_LIB_DIR="$WXWIN/lib/vc_dll"
WXWIN="/home/fcgle/source/ocpn_wxWidgets"

# For Opencpn 5.8 and wxWidgets-3.2.5

# Clean build directory
if [ -d "build" ]; then
    rm -rf build/*
else
    mkdir build
fi

cd build					  


# Run CMake configure inside MSVC environment
cmd.exe /c "$MSVC_ENV" ^&^& cmake -T v143 -A Win32 -DwxWidgets_ROOT=C:/Users/fcgle/source/ocpn_wxWidgets-3.2.2.1 -DOCPN_TARGET=MSVC ..

# Build plugin (RelWithDebInfo) inside MSVC environment
cmd.exe /c "$MSVC_ENV" ^&^& cmake --build . --config RelWithDebInfo

# Build installer (CPack)
cmd.exe /c "$MSVC_ENV" ^&^& cmake --build . --target package --config RelWithDebInfo

# Copy DLL/PDB to OpenCPN build tree
cp -uv ./RelWithDebInfo/*_pi.dll C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins
cp -uv ./RelWithDebInfo/*_pi.pdb C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins

# Upload to Cloudsmith (still Bash-native)
			 
bash ./cloudsmith-upload.sh

#if [ "$1" == "c" ]; then
#    cmake -T v143 -A Win32 -DOCPN_TARGET=MSVC ..
#    cmake --build . --target package --config relwithdebinfo >output.txt
#fi

# Alternative lines do the same thing.
# if [ "$1" == "c" ]; then
#     cmake -A Win32 -G "Visual Studio 17 2022" -DCMAKE_GENERATOR_PLATFORM=Win32 ..
#     cmake --build . --config Release
# fi

# Bash script completes tarball prep adding metadata into it.
# bash ./cloudsmith-upload.sh

# Example used to copy a plugin DLL for testing. Adjust the paths and plugin name.
#if [ "$1" == "pi" ]; then
#    cp /home/radar/AutoTrackRaymarine_pi/build/Release/autotrackraymarine_pi.dll  "/home/$USER/.local/share/opencpn/plugins"
#fi

# Find ${bold}"build/output.txt"${normal} file if the build is not successful.
# Other examples below.
