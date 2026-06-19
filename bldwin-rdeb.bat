REM!/bin/bash   NOW a Batch file

REM FE2 Testplugin
REM REKWITHDEBINFO VERSION
REM Use "./build-win.sh" to run cmake.
REM Adjust this command for your setup and Plugin.
REM Requires wxWidgets setup
REM - /home/fcgle/source/ocpn-wxWidgets
REM - /home/fcgle/source/ where all the plugins and OpenCPN repos are kept.
REM --------------------------------------
REM For Opencpn using MS Visual Studio 2022
REM --------------------------------------
REM Used for local builds and testing.
REM Create an empty "[plugin]/build" directory
REM Use Bash Prompt from the [plugin] root directory: "bash ./bldwin-rdeb.sh"
REM Find any errors in the build/output.txt file
REM Then use bash prompt to run cloudsmith-upload.sh command: "bash ./bldwin-rdeb.sh"
REM Which adds the metadata file to the tarball gz file.
REM Set local environment to find and use wxWidgets

REM Other OS have Curl packages installed.
REM For Windows, install vcpkg CURL and add to the cmake command
REM mkdir C:\vcpkg
REM cd C:\vcpkg
REM vcpkg install curl:x86-windows
REM vcpkg list
REM Does this exist? C:\vcpkg\installed\x86-windows\share\curl

REM Enable command tracing
set -x 

REM Confirm build exists and empty it and if no build directory create it.

if [ -d "build" ]; then
   echo "The 'build' direcREMtory exists, remove all build dir files."
   rm -rf build/*
else
   echo "The 'build' directory does not exist. Create the build directory"
   mkdir build
fi

REM wxWidgets settings with variables
REM set "wxWidgets_ROOT_DIR=C:\Users\fcgle\source\ocpn_wxWidgets"
REM set "OCPN_DIR=C:\Users\fcgle\source\opencpn\build"
REM cmake -G "Visual Studio 17 2022" ^
REM  -A Win32 ^
REM  -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR%
REM  -DOCPN_DIR=%OCPN_DIR%
REM  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
REM  ..
REM cmake --build . --target package --config relwithdebinfo > output.txt

REM wxDIR=$WXWIN
REM WXWIN="/home/fcgle/source/wxWidgets-3.2.2"
REM wxWidgets_ROOT_DIR=$WXWIN
REM wxWidgets_LIB_DIR="$WXWIN/lib/vc14x_dll"

REM change to build directory
cd build

REM CMake Build plugin (-T v143 is being used)
cmake -G "Visual Studio 17 2022" ^
  -A Win32 ^
  -DwxWidgets_ROOT_DIR=C:\Users\fcgle\source\ocpn_wxWidgets ^
  -DOCPN_DIR=C:\Users\fcgle\source\opencpn\build ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  ..
  
cmake --build . --target package --config relwithdebinfo > output.txt
	
REM Bash script completes tarball prep adding metadata into it.

bash ./cloudsmith-upload.sh


REM Find ${bold}"build/output.txt"${normal} file if the build is not successful.
REM Other examples below.
