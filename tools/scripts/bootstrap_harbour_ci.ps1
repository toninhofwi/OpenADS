# Bootstrap a minimal Harbour MSVC64 toolchain for CI.
# Installs into $InstallRoot (default: $env:RUNNER_TEMP\harbour-ci).
# Uses actions/cache keyed on this script's hash - first run builds from
# source (~45-90 min); subsequent runs reuse the cache.

param(
    [string]$InstallRoot = $(if ($env:HARBOUR_CI_ROOT) { $env:HARBOUR_CI_ROOT }
                             else { Join-Path $env:RUNNER_TEMP "harbour-ci" }),
    [string]$HarbourTag  = "3.2.0deb"
)

$ErrorActionPreference = "Stop"

$hbmk2 = Join-Path $InstallRoot "bin\win\msvc64\hbmk2.exe"
if (Test-Path $hbmk2) {
    Write-Host "[harbour-ci] Reusing cached toolchain at $InstallRoot"
    $env:HARBOUR_ROOT = $InstallRoot
    $hbBin = Join-Path $InstallRoot 'bin\win\msvc64'
    $env:PATH = "$hbBin;$env:PATH"
    exit 0
}

$src = Join-Path $env:RUNNER_TEMP "harbour-src"
if (-not (Test-Path (Join-Path $src ".git"))) {
    Write-Host "[harbour-ci] Cloning Harbour $HarbourTag ..."
    if (Test-Path $src) { Remove-Item -Recurse -Force $src }
    git clone --depth 1 --branch $HarbourTag `
        https://github.com/harbour/harbour.git $src
}

$build = Join-Path $src "build-ci"
Write-Host "[harbour-ci] Configuring CMake build in $build ..."
cmake -S $src -B $build -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_INSTALL_PREFIX=$InstallRoot `
    -DHB_BUILD_CONTRIBS=ON

Write-Host "[harbour-ci] Building Harbour (Release) - this may take a while ..."
cmake --build $build --config Release --target install

if (-not (Test-Path $hbmk2)) {
    Write-Error "[harbour-ci] hbmk2 not found after install: $hbmk2"
}

$env:HARBOUR_ROOT = $InstallRoot
$hbBin = Join-Path $InstallRoot 'bin\win\msvc64'
$env:PATH = "$hbBin;$env:PATH"
Write-Host "[harbour-ci] Ready: $hbmk2"