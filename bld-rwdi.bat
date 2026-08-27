@echo off
setlocal

REM ============================================================
REM  Build climatology_pi in RelWithDebInfo
REM  Using wxWidgets Release DLLs (/MD)
REM  Full plugin debugging, no wxWidgets debugging required
REM ============================================================


echo.
echo ==== LOAD MSVC x86 ENV: cl.exe, link.exe, nmake, and the correct CRT ====
echo.

call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x86
cl

echo ==== CLEANING OLD BUILD ====
echo.

rmdir /S /Q build 2>nul
del CMakeCache.txt 2>nul

echo.
echo ==== CONFIGURING (RelWithDebInfo, /MD) ====
echo.

cmake -A Win32 -T v143 ^
    -DOCPN_TARGET=MSVC ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -S . -B build

if errorlevel 1 (
    echo CONFIGURE FAILED
    exit /b 1
)

echo.
echo ==== BUILDING (RelWithDebInfo) ====
echo ==== Output sent to OUTPUT.txt ====
echo.

cmake --build build --config RelWithDebInfo --verbose > OUTPUT.TXT


if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo ==== Copy DLL + PDB into OpenCPN build tree ====
echo.

copy /Y .\build\RelWithDebInfo\*_pi.dll C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins

copy /Y .\build\RelWithDebInfo\*_pi.pdb C:\Users\fcgle\source\opencpn\build\RelWithDebInfo\plugins


echo.
echo ==== Build the tarball using CPACK ====
echo.

cmake --build build --config RelWithDebInfo --target package

echo.
echo ==== Run Cloudsmith upload script ====
echo.

call "%~dp0upload-cloudsmith.bat"

echo.
echo ============================================================
echo  Build complete.
echo  You can now debug climatology_pi in RelWithDebInfo.
echo ============================================================
echo.

endlocal
