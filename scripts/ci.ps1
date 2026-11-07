# scripts/ci.ps1 - Local CI runner for CapApp
$ErrorActionPreference = "Stop"

Write-Host "=== Running CapApp Continuous Integration Pipeline ===" -ForegroundColor Cyan

# 1. Clean build
& "$PSScriptRoot\build.bat" clean
if ($LASTEXITCODE -ne 0) {
    Write-Host "[CI FAILED] Build or unit tests failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

# 2. Run latency benchmark
Write-Host "`n=== Running Automated Latency Benchmark ===" -ForegroundColor Cyan
& "build\src\capapp.exe" --benchmark
if ($LASTEXITCODE -ne 0) {
    Write-Host "[CI FAILED] Latency benchmark failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "`n[CI SUCCESS] All builds, CTest suites, and benchmarks passed cleanly!" -ForegroundColor Green
