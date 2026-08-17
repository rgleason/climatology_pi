#!/bin/bash

# FE2 Testplugin
# REKWITHDEBINFO VERSION
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

# Enable command tracing

#!/bin/bash
set -x

# Clean build directory
if [ -d "build" ]; then
    rm -rf build/*
else
    mkdir build
fi

cd build

# Correct wxWidgets root directory
cmake -T v143 -A Win32 \
  -DwxWidgets_ROOT=/c/Users/fcgle/source/ocpn_wxWidgets-3.2.2.1 \
  -DOCPN_TARGET=MSVC \
  ..

# BUILD ONLY THE PLUGIN TARGET - whatever ${PACKAGE_NAME} is.
cmake --build . --config relwithdebinfo > ../output.txt

# STEP 2: Build the installer (CPack)
cmake --build . --target package --config relwithdebinfo

bash ./cloudsmith-upload.sh

cp -uv ./RelWithDebInfo/*_pi.dll C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins
cp -uv ./RelWithDebInfo/*_pi.pdb C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins


# Find ${bold}"build/output.txt"${normal} file if the build is not successful.
# Other examples below.

# Copy .dll and .pdb files for debugging into MSVisualStudio Development Setup
# Copy from 
# C:\Users\fcgle\source\weather_routing_pi\build\relwithdebinfo   weather_routing_pi.dll and weather_routing_pi.pdb
# into
# C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins


# cp -rv ./SourceFolder ./DestFolder
# cp -r ./dist/* ./out
#    -r - Copy all files and folders inside a directory
#    -i - Ask before replacing files
#    -u - Copy only if the source is newer
#    -v - Verbose mode, show files being copied
# copy ..\build\relwithdebinfo\weather_routing_pi.dll to  C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins
# copy ..\build\relwithdebinfo\weather_routing_pi.pdb to  C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins

cp -uv ./RelWithDebInfo/*_pi.dll C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins
cp -uv ./RelWithDebInfo/*_pi.pdb C:/Users/fcgle/source/opencpn/build/RelWithDebInfo/plugins