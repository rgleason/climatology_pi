@echo off
setlocal

REM --- Configure wxWidgets root ---
set "WXWIN=C:\Users\fcgle\source\ocpn_wxWidgets"

REM --- Derived wxWidgets paths ---
set "wxDIR=%WXWIN%"
set "wxWidgets_ROOT_DIR=%WXWIN%"
set "wxWidgets_LIB_DIR=%WXWIN%\lib\vc_dll"

REM --- Create build directory if missing ---
if not exist build mkdir build
cd build

REM --- Configure CMake for MSVC 2022 + Win32 + wxWidgets 3.2.2 ---
cmake -T v143 -A Win32 ^
  -DOCPN_TARGET=MSVC ^
  -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
  -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  ..

REM --- Build plugin tarball ---
cmake --build . --target tarball --config RelWithDebInfo > output.txt

endlocal
