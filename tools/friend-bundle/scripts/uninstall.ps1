# SkyrimReLive uninstaller. Removes our plugin DLL (and optionally the
# config file) from the Skyrim plugins directory. Does NOT touch SKSE,
# Address Library, or any other mod.

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$pathFile = Join-Path $ScriptDir 'skyrim_path.txt'

function Pause-Exit($code = 0) {
    Write-Host ''
    Read-Host 'Press Enter to close'
    exit $code
}
function Ask($q) { ($a = Read-Host "$q [y/N]") -match '^[Yy]' }

Write-Host ''
Write-Host '================================================' -ForegroundColor Cyan
Write-Host '  SkyrimReLive - uninstall' -ForegroundColor Cyan
Write-Host '================================================' -ForegroundColor Cyan
Write-Host ''

if (-not (Test-Path $pathFile)) {
    Write-Host 'No installed Skyrim path on file. Nothing to remove.' -ForegroundColor Yellow
    Pause-Exit
}
$SkyrimRoot = (Get-Content -Raw $pathFile).Trim()
$pluginsDir = Join-Path $SkyrimRoot 'Data\SKSE\Plugins'
$dll = Join-Path $pluginsDir 'SkyrimReLive.dll'
$toml = Join-Path $pluginsDir 'SkyrimReLive.toml'

Write-Host "Will remove from: $pluginsDir"
Write-Host '  - SkyrimReLive.dll'
Write-Host '  - SkyrimReLive.toml (your config — optional)'
Write-Host ''
Write-Host 'Will NOT touch SKSE, Address Library, or any other mod.'
Write-Host ''
if (-not (Ask 'Continue?')) { Pause-Exit }

if (Test-Path $dll) {
    Remove-Item -Force $dll
    Write-Host '  removed SkyrimReLive.dll' -ForegroundColor Green
} else {
    Write-Host '  SkyrimReLive.dll not present (already removed?)' -ForegroundColor Yellow
}

if (Test-Path $toml) {
    if (Ask 'Also remove SkyrimReLive.toml?') {
        Remove-Item -Force $toml
        Write-Host '  removed SkyrimReLive.toml' -ForegroundColor Green
    }
}

Write-Host ''
Write-Host 'Done. SkyrimReLive uninstalled.' -ForegroundColor Green
Pause-Exit
