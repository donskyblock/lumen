param(
    [string]$Triplet = "x64-windows",
    [string]$Configuration = "Release",
    [string]$BuildDir = "build-windows",
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$LumenArgs
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $env:LUMEN_VCPKG_ROOT) { $env:LUMEN_VCPKG_ROOT = Join-Path $RepoRoot ".deps\vcpkg" }
$Installed = Join-Path $env:LUMEN_VCPKG_ROOT "installed\$Triplet"
$Exe = Join-Path (Join-Path (Join-Path $RepoRoot $BuildDir) $Configuration) "lumen.exe"
if (-not (Test-Path $Exe)) { throw "Lumen is not built. Run .\build.ps1 first." }

$env:PATH = (Join-Path $Installed "bin") + ";" + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $Installed "plugins"
$env:QML_IMPORT_PATH = Join-Path $Installed "qml"
& $Exe @LumenArgs
exit $LASTEXITCODE
