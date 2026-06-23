param(
    [string]$BuildDir = "build"
)

Write-Host "=== CMake cache diagnostics ==="

# 1. Check if build directory exists
if (-Not (Test-Path $BuildDir)) {
    Write-Host "Build directory '$BuildDir' does not exist. No cache to diagnose."
    exit 0
}

# 2. Inspect CMakeCache.txt for wrong source dir or generator
$cachePath = Join-Path $BuildDir "CMakeCache.txt"
if (-Not (Test-Path $cachePath)) {
    Write-Host "No CMakeCache.txt found in '$BuildDir'."
    exit 0
}

Write-Host "Found CMakeCache.txt at $cachePath"
$cache = Get-Content $cachePath

# 3. Show key entries
$cache | Select-String -Pattern "CMAKE_HOME_DIRECTORY" | ForEach-Object {
    Write-Host "CMAKE_HOME_DIRECTORY =>" $_.Line
}
$cache | Select-String -Pattern "CMAKE_GENERATOR:" | ForEach-Object {
    Write-Host "CMAKE_GENERATOR =>" $_.Line
}
$cache | Select-String -Pattern "climatology_shaders.cpp" | ForEach-Object {
    Write-Host "Shader references in cache =>" $_.Line
}

# 4. Detect mismatch between current source dir and cache (literal compare)
$currentDir = (Get-Location).Path.Replace("\", "/")

$cachedHome = ($cache | Select-String -Pattern "CMAKE_HOME_DIRECTORY:INTERNAL=").Line
$cachedHomeValue = $cachedHome -replace "CMAKE_HOME_DIRECTORY:INTERNAL=", ""

if ($cachedHomeValue -eq $currentDir) {
    Write-Host "Source directory matches current location."
} else {
    Write-Host "WARNING: CMAKE_HOME_DIRECTORY in cache does NOT match current directory:"
    Write-Host "  Cached: $cachedHomeValue"
    Write-Host "  Current: $currentDir"
}


# 5. Suggest cleanup if suspicious
if ($cache -match "climatology_shaders.cpp" -and -Not (Test-Path "src\climatology_shaders.cpp")) {
    Write-Host "WARNING: Cache references climatology_shaders.cpp but file is missing in src."
    Write-Host "Consider: Remove-Item -Recurse -Force build"
}

Write-Host "=== Diagnostics complete ==="
