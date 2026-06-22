@echo off
setlocal enableDelayedExpansion

where cargo >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cargo not found on PATH — install Rust from https://rustup.rs/
    exit /b 1
)

cd /d "%~dp0"
echo [BUILD] openmonitor release...
cargo build --release %*
if errorlevel 1 exit /b 1
echo [OK] target\release\openmonitor.exe
exit /b 0