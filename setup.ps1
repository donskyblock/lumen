param(
    [string]$Triplet = "x64-windows"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$VcpkgRoot = if ($env:LUMEN_VCPKG_ROOT) { $env:LUMEN_VCPKG_ROOT } else { Join-Path $RepoRoot ".deps\vcpkg" }
$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git is required. Install Git for Windows, then rerun setup.ps1."
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake is required. Install CMake and the Visual Studio 2022 C++ workload, then rerun setup.ps1."
}

if (-not (Test-Path $VcpkgExe)) {
    if (Test-Path $VcpkgRoot) {
        throw "No vcpkg.exe found in $VcpkgRoot. Set LUMEN_VCPKG_ROOT to an existing vcpkg checkout, or remove the incomplete .deps\vcpkg folder and rerun."
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $VcpkgRoot) | Out-Null
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw "Could not clone vcpkg." }
    & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "Could not bootstrap vcpkg." }
}

& $VcpkgExe install "qtbase:$Triplet" "qtdeclarative:$Triplet" "ffmpeg:$Triplet" "sqlite3:$Triplet" "zlib:$Triplet"
if ($LASTEXITCODE -ne 0) { throw "Dependency installation failed." }
Write-Host "Windows dependencies are ready. Run .\build.ps1 to compile Lumen."
