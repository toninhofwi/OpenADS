<#
  Firebird ODBC fixture — writable paths stay inside the OpenADS repo.
  Optional read-only: OPENADS_FB_CLIENT_HOME for isql/fbclient (one-shot).

  Dot-source after setting $OpenAdsRepoRoot (repo root).

  Env (opcional):
    OPENADS_FB_SOURCE_DB     caminho do .fdb fonte para copiar 1x (so leitura)
    OPENADS_FB_CLIENT_HOME   portable Firebird install (isql.exe, firebird.exe — read-only)
    OPENADS_FB_PASSWORD      senha SYSDBA (default masterkey)
    OPENADS_FB_ODBC_DRIVER   nome do driver ODBC (auto-detect se omitido)
#>
function Get-OpenAdsRepoRoot {
    if ($script:OpenAdsRepoRoot) { return $script:OpenAdsRepoRoot }
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}

function Get-OpenAdsFirebirdClientHome {
    if ($env:OPENADS_FB_CLIENT_HOME -and (Test-Path $env:OPENADS_FB_CLIENT_HOME)) {
        return $env:OPENADS_FB_CLIENT_HOME
    }
    if ($env:FIREBIRD -and (Test-Path $env:FIREBIRD)) {
        return $env:FIREBIRD
    }
    return $null
}

function Ensure-OpenAdsFirebirdServer {
    param([string]$ClientHome)
    if (-not $ClientHome) { return $false }
    $fbExe = Join-Path $ClientHome "firebird.exe"
    if (-not (Test-Path $fbExe)) { return $false }

    $running = Get-Process -Name firebird -ErrorAction SilentlyContinue
    if ($running) { return $true }

    $env:FIREBIRD = $ClientHome
    $env:PATH = "$ClientHome;$env:PATH"
    Start-Process -FilePath $fbExe -ArgumentList "-a" -WindowStyle Minimized | Out-Null
    Start-Sleep -Seconds 2
    return $true
}

function New-OpenAdsFirebirdDatabase {
    param(
        [string]$RepoRoot,
        [string]$ClientHome,
        [string]$Password
    )
    $isql = Join-Path $ClientHome "isql.exe"
    if (-not (Test-Path $isql)) {
        Write-Warning "isql.exe not found under OPENADS_FB_CLIENT_HOME"
        return $false
    }

    $fixture = Join-Path $RepoRoot "tests\fixtures\firebird"
    $dbPath  = Join-Path $fixture "openads_fixture.fdb"
    New-Item -ItemType Directory -Force -Path (Join-Path $fixture "runtime") | Out-Null
    if (Test-Path $dbPath) { return $true }

    if (-not (Ensure-OpenAdsFirebirdServer -ClientHome $ClientHome)) {
        Write-Warning "firebird.exe unavailable — cannot CREATE DATABASE in-repo"
        return $false
    }

    $dbSql = $dbPath -replace '\\', '/'
    $createSql = @(
        "CREATE DATABASE '$dbSql' USER 'SYSDBA' PASSWORD '$Password' DEFAULT CHARACTER SET UTF8;"
        "COMMIT;"
        "EXIT;"
    ) -join "`n"

    $tmp = Join-Path $fixture "_create_db.tmp.sql"
    Set-Content -Path $tmp -Value $createSql -Encoding ASCII
    try {
        $env:FIREBIRD = $ClientHome
        $env:PATH = "$ClientHome;$env:PATH"
        & $isql -user SYSDBA -password $Password -i $tmp
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "CREATE DATABASE failed (exit $LASTEXITCODE)"
            return $false
        }
        Write-Host "[fixture] created FDB in repo: tests/fixtures/firebird/openads_fixture.fdb"
        return (Test-Path $dbPath)
    } finally {
        Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    }
}

function Seed-OpenAdsFirebirdDatabase {
    param(
        [string]$RepoRoot,
        [string]$ClientHome,
        [string]$Password
    )
    $fx = Initialize-OpenAdsFirebirdFixture -RepoRoot $RepoRoot
    if (-not (Test-Path $fx.Database)) { return $false }

    $seed = Join-Path $RepoRoot "tools\scripts\seed_nav_clientes_firebird.sql"
    if (-not (Test-Path $seed)) {
        Write-Warning "seed_nav_clientes_firebird.sql missing"
        return $false
    }

    $isql = Join-Path $ClientHome "isql.exe"
    if (-not (Test-Path $isql)) { return $false }

    Ensure-OpenAdsFirebirdServer -ClientHome $ClientHome | Out-Null
    $env:FIREBIRD = $ClientHome
    $env:PATH = "$ClientHome;$env:PATH"
    & $isql -user SYSDBA -password $Password $fx.Database -i $seed
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "seed failed (exit $LASTEXITCODE)"
        return $false
    }
    Write-Host "[fixture] seeded clientes table"
    return $true
}

function Initialize-OpenAdsFirebirdFixture {
    param(
        [string]$RepoRoot = (Get-OpenAdsRepoRoot)
    )
    $fixture = Join-Path $RepoRoot "tests\fixtures\firebird"
    $runtime = Join-Path $fixture "runtime"
    $dbPath  = Join-Path $fixture "openads_fixture.fdb"
    New-Item -ItemType Directory -Force -Path $runtime | Out-Null

    $password = if ($env:OPENADS_FB_PASSWORD) { $env:OPENADS_FB_PASSWORD } else { "masterkey" }

    if (-not (Test-Path $dbPath)) {
        $src = $env:OPENADS_FB_SOURCE_DB
        if ($src -and (Test-Path $src)) {
            Copy-Item $src $dbPath -Force
            Write-Host "[fixture] copied FDB into repo: tests/fixtures/firebird/openads_fixture.fdb"
        }
    }

    $gdsLocal = Join-Path $runtime "gds32.dll"
    if (-not (Test-Path $gdsLocal)) {
        $clientHome = Get-OpenAdsFirebirdClientHome
        if ($clientHome) {
            $fbclient = Join-Path $clientHome "fbclient.dll"
            if (Test-Path $fbclient) {
                Copy-Item $fbclient $gdsLocal -Force
                Write-Host "[fixture] copied fbclient -> tests/fixtures/firebird/runtime/gds32.dll"
            }
        }
    }

    return @{
        FixtureDir = $fixture
        RuntimeDir = $runtime
        Database   = $dbPath
        Gds32      = $gdsLocal
        Password   = $password
    }
}

function Prepare-OpenAdsFirebirdOdbcRuntime {
    param([string]$RepoRoot = (Get-OpenAdsRepoRoot))

    $fx = Initialize-OpenAdsFirebirdFixture -RepoRoot $RepoRoot
    if (-not (Test-Path $fx.Database)) { return $null }

    if (Test-Path $fx.Gds32) {
        $env:PATH = "$($fx.RuntimeDir);$env:PATH"
    }
    $clientHome = Get-OpenAdsFirebirdClientHome
    if ($clientHome) {
        Ensure-OpenAdsFirebirdServer -ClientHome $clientHome | Out-Null
        $env:PATH = "$clientHome;$env:PATH"
    }
    return $fx
}

function Resolve-OpenAdsFirebirdConnStrLocal {
    param([string]$RepoRoot = (Get-OpenAdsRepoRoot))

    $fx = Initialize-OpenAdsFirebirdFixture -RepoRoot $RepoRoot
    if (-not (Test-Path $fx.Database)) { return $null }

    $drivers = Get-OdbcDriver -Platform "64-bit" -ErrorAction SilentlyContinue
    $driverName = $env:OPENADS_FB_ODBC_DRIVER
    if (-not $driverName) {
        if ($drivers.Name -contains "Firebird ODBC Driver") {
            $driverName = "Firebird ODBC Driver"
        } else {
            $driverName = $drivers.Name | Where-Object { $_ -match 'Firebird' } | Select-Object -First 1
        }
    }
    if (-not $driverName -or $drivers.Name -notcontains $driverName) { return $null }

    if (Test-Path $fx.Gds32) {
        $env:PATH = "$($fx.RuntimeDir);$env:PATH"
    } elseif ($env:OPENADS_FB_CLIENT_HOME -and (Test-Path $env:OPENADS_FB_CLIENT_HOME)) {
        $env:PATH = "$($env:OPENADS_FB_CLIENT_HOME);$env:PATH"
    }

    return "Driver={$driverName};Database=$($fx.Database);Uid=SYSDBA;Pwd=$($fx.Password);Charset=UTF8;"
}