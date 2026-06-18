<#
    build-curl-win32.ps1
    Reproducible 32-bit Windows build of libcurl.dll for OpenCPN plugins
#>

#  To build  
#      output32\libcurl.lib
#      output32\libcurl.dll
#      output32\include\curl\*.h

#  Using Developer Powershell for VS 2022
#  From:  C:\Users\fcgle\source\climatology_pi\win_curl
#  Execute: cmake --install C:\Users\fcgle\source\climatology_pi\win_curl\build32\curl\build32 --config Release --prefix C:\Users\fcgle\source\climatology_pi\win_curl\output32



# -----------------------------
# Configuration
# -----------------------------
$OpenSSLVersion = "OpenSSL_1_1_1w"
$CurlVersion    = "curl-8_7_1"

$WorkDir = "$PSScriptRoot\build32"
$InstallRoot = "$WorkDir\install"

$OpenSSLDir = "$WorkDir\openssl"
$CurlDir    = "$WorkDir\curl"

# Ensure directories exist
New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null
New-Item -ItemType Directory -Force -Path $InstallRoot | Out-Null

# -----------------------------
# Helper: Run commands safely
# -----------------------------
function Run($cmd) {
    Write-Host ">> $cmd" -ForegroundColor Cyan
    & cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Command failed: $cmd" -ForegroundColor Red
        exit 1
    }
}

# -----------------------------
# Step 1: Clone OpenSSL
# -----------------------------
if (!(Test-Path $OpenSSLDir)) {
    Run "git clone https://github.com/openssl/openssl.git `"$OpenSSLDir`""
}

Push-Location $OpenSSLDir
Run "git checkout $OpenSSLVersion"

# -----------------------------
# Step 2: Build OpenSSL (32-bit static)
# -----------------------------
Run "perl Configure VC-WIN32 --prefix=$InstallRoot\openssl --openssldir=$InstallRoot\openssl"
Run "nmake"
Run "nmake install"

Pop-Location

# -----------------------------
# Step 3: Clone curl
# -----------------------------
if (!(Test-Path $CurlDir)) {
    Run "git clone https://github.com/curl/curl.git `"$CurlDir`""
}

Push-Location $CurlDir
Run "git checkout $CurlVersion"

# -----------------------------
# Step 4: Configure curl with CMake (Win32)
# -----------------------------
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

# -----------------------------
# Step 5: Build curl (Win32)
# -----------------------------
Run "cmake --build `"$CurlBuild`" --config Release"

# -----------------------------
# Step 6: Install curl (creates libcurl.lib)
# -----------------------------
$OutDir = "$PSScriptRoot\output32"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Run "cmake --install `"$CurlBuild`" --config Release --prefix `"$OutDir`""

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "32-bit Build complete!"
Write-Host "libcurl.dll and libcurl.lib are located in: $OutDir" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
