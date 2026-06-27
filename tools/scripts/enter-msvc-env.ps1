$ErrorActionPreference = 'Stop'

$vsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$vsDevCmd = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$vsInstaller = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$cmakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'

if (!(Test-Path -LiteralPath $vsDevCmd)) {
    throw "VsDevCmd.bat not found at $vsDevCmd"
}

$oldPath = $env:Path
$env:Path = "$vsInstaller;$oldPath"
$cmd = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
$envDump = & cmd.exe /d /c $cmd
foreach ($line in $envDump) {
    $idx = $line.IndexOf('=')
    if ($idx -le 0) { continue }
    $name = $line.Substring(0, $idx)
    $value = $line.Substring($idx + 1)
    Set-Item -Path "Env:$name" -Value $value
}

$prepend = @($cmakeBin, $ninjaBin, $vsInstaller) | Where-Object { Test-Path -LiteralPath $_ }
$existing = $env:Path -split ';'
$env:Path = (($prepend + $existing) | Where-Object { $_ } | Select-Object -Unique) -join ';'

Write-Host "MSVC x64 build environment loaded."
Write-Host "Use: cmake, ninja, cl, msbuild"
