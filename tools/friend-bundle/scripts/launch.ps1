# SkyrimReLive launch helper. Reads the Skyrim path saved by install.ps1
# and runs skse64_loader.exe (which Skyrim must be launched through).

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$pathFile = Join-Path $ScriptDir 'skyrim_path.txt'

function Pause-Exit($code = 0) {
    Write-Host ''
    Read-Host 'Press Enter to close'
    exit $code
}

if (-not (Test-Path $pathFile)) {
    Write-Host '[ERROR] No Skyrim path saved. Run INSTALL.bat first.' -ForegroundColor Red
    Pause-Exit 1
}

$SkyrimRoot = (Get-Content -Raw $pathFile).Trim()
$loader = Join-Path $SkyrimRoot 'skse64_loader.exe'
if (-not (Test-Path $loader)) {
    Write-Host "[ERROR] skse64_loader.exe not found at $loader" -ForegroundColor Red
    Write-Host 'Re-run INSTALL.bat (your Skyrim install may have moved).'
    Pause-Exit 1
}

# Quick sanity: did the user remember to start Tailscale?
$tailscaleExe = 'C:\Program Files\Tailscale\tailscale.exe'
if (Test-Path $tailscaleExe) {
    $st = & $tailscaleExe status 2>&1
    if ($st -match 'Logged out|stopped|tailscaled is not running') {
        Write-Host '[WARN] Tailscale is not signed in.' -ForegroundColor Yellow
        Write-Host '       The plugin will fail to connect to the server.'
        Write-Host '       Open the Tailscale tray icon and sign in, then try again.'
        Write-Host ''
        $a = Read-Host 'Launch Skyrim anyway? [y/N]'
        if ($a -notmatch '^[Yy]') { exit 1 }
    }
}

Write-Host "Launching: $loader"
Start-Process -FilePath $loader -WorkingDirectory $SkyrimRoot
Write-Host 'Skyrim is starting. You can close this window.'
Start-Sleep -Seconds 3
