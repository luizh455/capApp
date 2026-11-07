# scripts/build.ps1 - CapApp build automation helper
param(
    [string]$Config = "Release",
    [switch]$RunTests,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

if ($Clean -and (Test-Path "build")) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force "build"
}

# Locate Visual Studio vcvarsall.bat if cl is not in path
if (-not (Get-Command "cl.exe" -ErrorAction SilentlyContinue)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsInstallPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsInstallPath) {
            $vcvars = Join-Path $vsInstallPath "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $vcvars) {
                Write-Host "Initializing MSVC environment from $vcvars" -ForegroundColor Cyan
                cmd.exe /c "call `"$vcvars`" && set" | ForEach-Object {
                    if ($_ -match "^(.*?)=(.*)$") {
                        Set-Content "env:$($matches[1])" $matches[2]
                    }
                }
            }
        }
    }
}

Write-Host "Configuring CMake (Ninja, $Config)..." -ForegroundColor Cyan
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=$Config -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded

Write-Host "Building target (Config: $Config)..." -ForegroundColor Cyan
cmake --build build --config $Config

if ($RunTests) {
    Write-Host "Running tests via CTest..." -ForegroundColor Cyan
    ctest --test-dir build -C $Config --output-on-failure
}
