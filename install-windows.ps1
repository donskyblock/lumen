param(
    [string]$Triplet = "x64-windows",
    [string]$Configuration = "Release",
    [string]$BuildDir = "build-windows",
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA "Programs\Lumen")
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $env:LUMEN_VCPKG_ROOT) { $env:LUMEN_VCPKG_ROOT = Join-Path $RepoRoot ".deps\vcpkg" }
$Installed = Join-Path $env:LUMEN_VCPKG_ROOT "installed\$Triplet"
$Exe = Join-Path (Join-Path (Join-Path $RepoRoot $BuildDir) $Configuration) "lumen.exe"
if (-not (Test-Path $Exe)) { throw "Lumen is not built. Run .\build.ps1 first." }
if (-not (Test-Path $Installed)) { throw "vcpkg dependencies are missing. Run .\setup.ps1 first." }

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item -Force $Exe (Join-Path $InstallDir "lumen.exe")
$pluginDir = Join-Path $InstallDir "plugins"
New-Item -ItemType Directory -Force -Path $pluginDir | Out-Null
Copy-Item -Force (Join-Path (Join-Path $RepoRoot "plugins") "*") $pluginDir -Recurse
foreach ($dirName in @("bin", "plugins", "qml")) {
    $source = Join-Path $Installed $dirName
    if (Test-Path $source) {
        $destination = if ($dirName -eq "plugins") { $pluginDir } elseif ($dirName -eq "qml") { Join-Path $InstallDir "qml" } else { $InstallDir }
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        Copy-Item -Force (Join-Path $source "*") $destination -Recurse
    }
}

$launcher = Join-Path $InstallDir "Lumen.cmd"
@'
@echo off
set "PATH=%~dp0;%PATH%"
set "QT_PLUGIN_PATH=%~dp0plugins"
set "QML_IMPORT_PATH=%~dp0qml"
"%~dp0lumen.exe" %*
'@ | Set-Content -Encoding Ascii $launcher

$startMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
New-Item -ItemType Directory -Force -Path $startMenu | Out-Null
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path $startMenu "Lumen.lnk"))
$shortcut.TargetPath = $launcher
$shortcut.WorkingDirectory = $InstallDir
$shortcut.Description = "Lumen music player"
$shortcut.Save()
Write-Host "Installed Lumen to $InstallDir and added a Start Menu shortcut."
