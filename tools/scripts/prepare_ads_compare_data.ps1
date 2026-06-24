<#
  Create bench.dbf + bench.cdx with SAP ADS (ace64) for engine A/B compare.
  Usage: pwsh tools/scripts/prepare_ads_compare_data.ps1 [-Rows 10000] [-Build]
#>
param(
    [string]$DestDir = "",
    [int]$Rows = 10000,
    [switch]$Build
)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "ads_bench_common.ps1")

$repo = Get-OpenAdsRepoRoot
$compare = Join-Path $repo "tools\bench\ads_compare"
$bin = Join-Path $compare "bin"
if (-not $DestDir) {
    $DestDir = Join-Path $repo "tools\bench\fixtures\ads_compare_data"
}
New-Item -ItemType Directory -Force -Path $DestDir | Out-Null

$destDbf = Join-Path $DestDir "bench.dbf"
$destCdx = Join-Path $DestDir "bench.cdx"
if ((Test-Path $destDbf) -and -not $Build) {
    Write-Host "[data] reuse $DestDir"
    exit 0
}

$prepareExe = Join-Path $bin "ads_prepare_sap_64.exe"
if ($Build -or -not (Test-Path $prepareExe)) {
    $libs = Resolve-AdsEngineLibPaths
    # Toolchain locations come from env (OPENADS_HB_BIN / OPENADS_MSVC_SETUP /
    # MSVC_UCRT_X64); build_engine_bench.bat validates them and errors if unset.
    $env:ACE64_LIB_PATH = $libs.AceLibDir
    $env:OPENACE64_LIB = $libs.OpenLib
    Push-Location $compare
    try {
        & cmd /c build_engine_bench.bat
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    } finally {
        Pop-Location
    }
}

if (-not (Test-Path $prepareExe)) {
    Write-Error "ads_prepare_sap_64.exe missing. Run with -Build."
}

$sapSdk = Resolve-AdsSapSdkDir
$sapDll = Resolve-AdsSapDllDir
$stage = Join-Path $env:TEMP "ads_prepare_bench"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-AdsSupportFiles -DestDir $stage -SdkDir $sapSdk
Get-ChildItem $sapDll -Filter "*.dll" | Copy-Item -Destination $stage -Force
Copy-Item $prepareExe $stage -Force

$workData = Join-Path $stage "data"
New-Item -ItemType Directory -Path $workData -Force | Out-Null

Push-Location $stage
try {
    & .\ads_prepare_sap_64.exe $workData $Rows 2>&1
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}

$srcDbf = Join-Path $workData "bench.dbf"
$srcCdx = Join-Path $workData "bench.cdx"
if (-not (Test-Path $srcDbf)) {
    Write-Error "SAP prepare did not create bench.dbf in $workData"
}
Remove-Item (Join-Path $DestDir "bench.dbf"), (Join-Path $DestDir "bench.cdx"),
    (Join-Path $DestDir "openads.txlog"), (Join-Path $DestDir "openads.lsnmap") -Force -ErrorAction SilentlyContinue
Copy-Item $srcDbf (Join-Path $DestDir "bench.dbf") -Force
if (Test-Path $srcCdx) {
    Copy-Item $srcCdx (Join-Path $DestDir "bench.cdx") -Force
}
$destDbf = Join-Path $DestDir "bench.dbf"
$destCdx = Join-Path $DestDir "bench.cdx"
Write-Host "[data] SAP bench rows=$Rows -> $DestDir (cdx=$(Test-Path $destCdx))"
exit 0