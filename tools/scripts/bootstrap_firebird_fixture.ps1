<#
  One-shot: prepare Firebird ODBC fixture inside the repo (writable paths only).

  Usage (from repo root):
    pwsh tools/scripts/bootstrap_firebird_fixture.ps1

  Optional — read-only external sources:
    $env:OPENADS_FB_SOURCE_DB = '...\openads_fixture.fdb'
    $env:OPENADS_FB_CLIENT_HOME = '...\firebird'   # isql + fbclient + firebird.exe
    $env:OPENADS_FB_PASSWORD = 'masterkey'
#>
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $PSScriptRoot "fixture_firebird_local.ps1")
$script:OpenAdsRepoRoot = $repo

$clientHome = Get-OpenAdsFirebirdClientHome
$password = if ($env:OPENADS_FB_PASSWORD) { $env:OPENADS_FB_PASSWORD } else { "masterkey" }

$fx = Initialize-OpenAdsFirebirdFixture -RepoRoot $repo
if (-not (Test-Path $fx.Database)) {
    if ($clientHome) {
        if (-not (New-OpenAdsFirebirdDatabase -RepoRoot $repo -ClientHome $clientHome -Password $password)) {
            Write-Error "Could not create openads_fixture.fdb in-repo. Set OPENADS_FB_SOURCE_DB or OPENADS_FB_CLIENT_HOME."
        }
        $fx = Initialize-OpenAdsFirebirdFixture -RepoRoot $repo
    } else {
        Write-Error "openads_fixture.fdb missing. Set OPENADS_FB_SOURCE_DB or OPENADS_FB_CLIENT_HOME and re-run."
    }
}

if ($clientHome) {
    Seed-OpenAdsFirebirdDatabase -RepoRoot $repo -ClientHome $clientHome -Password $password | Out-Null
}

if (-not (Test-Path $fx.Gds32)) {
    Write-Warning "runtime/gds32.dll missing — set OPENADS_FB_CLIENT_HOME (read-only) and re-run"
}

Write-Host "OK fixture:" $fx.Database