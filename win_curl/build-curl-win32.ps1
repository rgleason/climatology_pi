<#
    build-curl-win32.ps1
    Fully reproducible Win32 build of:
      - zlib (static)
      - OpenSSL (static)
      - curl (DLL using OpenSSL + zlib)
#>

#  Using Developer Powershell for VS 2022   (type: Dev Powershell..)
#  Must run from correct directory or everything lands in wrond dirs.
#  cd C:\Users\fcgle\source\climatology_pi\win_curl
#  powershell -ExecutionPolicy Bypass -File build-curl-win32.ps1

# Delete all subdirectories before running!
# build32\
# output32\
# build32\zlib\
# build32\openssl\
# build32\curl\
# build32\install\

# Produces
# output32\libcurl.dll
# output32\lib\libcurl_imp.lib
# output32\include\curl\*.h
# output32\zlib\*.h
# output32\openssl\*.h

<#
Copy-Paste this script into
    win_curl/build-curl-win32.ps1

What this script now guarantees
 1.zlib is built
 2.OpenSSL is built
 3.curl is built with:
 4.OpenSSL
 5.zlib
 6.NO WinSSL
 7.NO Schannel
 8.curl is installed into:
	output32/
		libcurl.dll
		lib/libcurl_imp.lib
		include/curl/*.h
		zlib/*.h
		openssl/*.h
 9.MSVC can link your plugin
 10.Your plugin can perform HTTPS
 11.Your plugin can handle gzip/deflate HTTP compression
 12.Everything is reproducible
#>

# -----------------------------
# Versions
# -----------------------------
$OpenSSLVersion = "OpenSSL_1_1_1w"
$CurlVersion    = "curl-8_7_1"
$ZlibVersion    = "v1.3.1"

# -----------------------------
# Directories
# -----------------------------
$WorkDir     = "$PSScriptRoot\build32"
$InstallRoot = "$WorkDir\install"

$ZlibDir     = "$WorkDir\zlib"
$OpenSSLDir  = "$WorkDir\openssl"
$CurlDir     = "$WorkDir\curl"

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null

# -----------------------------
# Helper
# -----------------------------
function Run($cmd) {
    Write-Host ">> $cmd" -ForegroundColor Cyan
    & cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Command failed: $cmd" -ForegroundColor Red
        exit 1
    }
}

# ================================================================
# Step 1 — Build zlib (static)
# ================================================================
if (!(Test-Path $ZlibDir)) {
    Run "git clone https://github.com/madler/zlib.git `"$ZlibDir`""
}

Push-Location $ZlibDir
Run "git checkout $ZlibVersion"

$ZlibBuild = "$ZlibDir\build32"
New-Item -ItemType Directory -Force -Path $ZlibBuild | Out-Null

Run "cmake -G `"Visual Studio 17 2022`" -A Win32 -B `"$ZlibBuild`" -DCMAKE_INSTALL_PREFIX=`"$InstallRoot\zlib`""
Run "cmake --build `"$ZlibBuild`" --config Release"
Run "cmake --install `"$ZlibBuild`" --config Release"

Pop-Location

# ================================================================
# Step 2 — Build OpenSSL (static)
# ================================================================
if (!(Test-Path $OpenSSLDir)) {
    Run "git clone https://github.com/openssl/openssl.git `"$OpenSSLDir`""
}

Push-Location $OpenSSLDir
Run "git checkout $OpenSSLVersion"

Run "perl Configure VC-WIN32 --prefix=$InstallRoot\openssl --openssldir=$InstallRoot\openssl"
Run "nmake"
Run "nmake install"

Pop-Location

# ================================================================
# Step 3 — Build curl (DLL using OpenSSL + zlib)
# ================================================================
if (!(Test-Path $CurlDir)) {
    Run "git clone https://github.com/curl/curl.git `"$CurlDir`""
}

Push-Location $CurlDir
Run "git checkout $CurlVersion"

$CurlBuild = "$CurlDir\build32"
New-Item -ItemType Directory -Force -Path $CurlBuild | Out-Null

Run "cmake -G `"Visual Studio 17 2022`" -A Win32 -B `"$CurlBuild`" `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_SHARED_LIBS=ON `
    -DCURL_USE_OPENSSL=ON `
    -DOPENSSL_ROOT_DIR=`"$InstallRoot\openssl`" `
    -DCURL_ZLIB=ON `
    -DZLIB_ROOT=`"$InstallRoot\zlib`" `
    -DCURL_USE_SCHANNEL=OFF `
    -DCURL_USE_WINSSL=OFF `
    -DBUILD_CURL_EXE=OFF"

Run "cmake --build `"$CurlBuild`" --config Release"

Pop-Location

# ================================================================
# Step 4 — Install curl (DLL + import lib + headers)
# ================================================================
$OutDir = "$PSScriptRoot\output32"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Run "cmake --install `"$CurlBuild`" --config Release --prefix `"$OutDir`""

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "32-bit Build complete!"
Write-Host "Output located in: $OutDir" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
