param(
    [string]$Triplet = "x64-windows",
    [string]$Configuration = "Release",
    [string]$BuildDir = "build-windows"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $env:LUMEN_VCPKG_ROOT) { $env:LUMEN_VCPKG_ROOT = Join-Path $RepoRoot ".deps\vcpkg" }
$VcpkgToolchain = Join-Path $env:LUMEN_VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $VcpkgToolchain)) {
    throw "vcpkg is not set up. Run .\setup.ps1 first or set LUMEN_VCPKG_ROOT to your vcpkg checkout."
}
$BuildPath = Join-Path $RepoRoot $BuildDir

cmake -S $RepoRoot -B $BuildPath -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain" "-DVCPKG_TARGET_TRIPLET=$Triplet"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
cmake --build $BuildPath --config $Configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Lumen build failed." }

Write-Host "Built $BuildPath\$Configuration\lumen.exe"
Write-Host "Run it with .\run.ps1"
