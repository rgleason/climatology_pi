@echo off
setlocal

REM ============================================================
REM  Build climatology_pi in RelWithDebInfo
REM  Using wxWidgets Release DLLs (/MD)
REM  Full plugin debugging, no wxWidgets debugging required
REM ============================================================

echo.
echo ===== CLEANING OLD BUILD =====
echo.

rmdir /S /Q build 2>nul
del CMakeCache.txt 2>nul

echo.
echo ===== CONFIGURING (RelWithDebInfo, /MD) =====
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
echo ===== BUILDING (RelWithDebInfo) =====
echo.

cmake --build build --config RelWithDebInfo

if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)

echo.
echo ============================================================
echo  Build complete.
echo  You can now debug climatology_pi in RelWithDebInfo.
echo ============================================================
echo.

endlocal
