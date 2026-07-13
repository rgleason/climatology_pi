@echo off
setlocal

pushd "%~dp0" 
echo AFTER PUSHD: %CD%


REM --- 1. Load MSVC environment (VS 2022 x86) ---
rem call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
rem call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"
echo AFTER VCVARS: %CD%


REM --- 2. Kill stale processes ---
taskkill /IM opencpn.exe /F >nul 2>&1
taskkill /IM cl.exe /F >nul 2>&1
taskkill /IM link.exe /F >nul 2>&1
taskkill /IM mspdbsrv.exe /F >nul 2>&1
taskkill /IM cmake.exe /F >nul 2>&1
taskkill /IM ninja.exe /F >nul 2>&1

REM --- 3. Clean build directory ---
if exist build (
    rmdir /S /Q build
)

REM --- 4a. Configure with Visual Studio generator (recommended for MSVC) ---
echo BEFORE CMAKE: %CD%
cmake -B build -G "Visual Studio 17 2022" ^
-A Win32 ^
-DCMAKE_BUILD_TYPE=RelWithDebInfo ^
-DwxWidgets_ROOT_DIR="C:/Users/fcgle/source/ocpn_wxWidgets" ^
"C:/Users/fcgle/source/climatology_pi"  
rem ..

	
if errorlevel 1 (
    echo CMake configure failed.
	popd
    exit /b 1
)

REM --- 5. Build ---
cmake --build build --config RelWithDebInfo > OUTPUT.TXT
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

REM --- 6. Copy plugin DLL to OpenCPN plugins folder (adjust path) ---
set PLUG_DLL=build\RelWithDebInfo\climatology_pi.dll
set OCPN_PLUGINS=C:\Program Files (x86)\OpenCPN\plugins

if exist "%PLUG_DLL%" (
    copy /Y "%PLUG_DLL%" "%OCPN_PLUGINS%\climatology_pi.dll"
)

echo Build and deploy complete.
endlocal
