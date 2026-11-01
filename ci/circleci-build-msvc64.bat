@echo off
setlocal

rem Resolve the installed VS edition and select its native x64 tools.
set "CLIMATOLOGY_VS_ROOT="
for /f "usebackq delims=" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CLIMATOLOGY_VS_ROOT=%%i"
if not defined CLIMATOLOGY_VS_ROOT (
    echo Visual Studio x64 C++ tools were not found.
    exit /b 1
)
call "%CLIMATOLOGY_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set "PATH=C:\Program Files\Poedit\GettextTools\bin;C:\Program Files\7-Zip;%PATH%"
python --version
if errorlevel 1 exit /b 1
python -m unittest discover -s ci\public-packages\tests -p test_windows_abi.py
if errorlevel 1 exit /b 1
python ci\public-packages\windows64.py
exit /b %errorlevel%
