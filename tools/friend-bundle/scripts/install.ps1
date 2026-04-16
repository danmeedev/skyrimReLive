# SkyrimReLive friend installer.
# Run from INSTALL.bat. Walks through Skyrim detection, dependency checks,
# plugin install, and a connectivity probe to the host's server.

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$BundleRoot = Split-Path -Parent $ScriptDir
$SrcDir = Join-Path $BundleRoot 'files'
$HostAddr = '{{HOST_IP}}'
$HostPort = 27015
$HostName = '{{HOST_NAME}}'

function Banner($msg) {
    Write-Host ''
    Write-Host '================================================' -ForegroundColor Cyan
    Write-Host "  $msg" -ForegroundColor Cyan
    Write-Host '================================================' -ForegroundColor Cyan
    Write-Host ''
}
function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "    [OK] $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "    [WARN] $msg" -ForegroundColor Yellow }
function Bad($msg)  { Write-Host "    [FAIL] $msg" -ForegroundColor Red }
function Pause-Exit($code = 0) {
    Write-Host ''
    Read-Host 'Press Enter to close this window'
    exit $code
}
function Ask($question) {
    $a = Read-Host "$question [Y/n]"
    return ($a -eq '' -or $a -match '^[Yy]')
}

Banner 'SkyrimReLive - friend install'

Write-Host 'This installer will:'
Write-Host '  1. Find your Skyrim Special Edition install'
Write-Host '  2. Check that SKSE64, Address Library, and Tailscale are installed'
Write-Host "  3. Copy the SkyrimReLive plugin into Skyrim's Data folder"
Write-Host "  4. Test that you can reach $HostName's server"
Write-Host ''
Write-Host 'You will need to download a couple of mods if you do not have'
Write-Host 'them already. The installer will pop the right pages open for you.'
Write-Host ''
if (-not (Ask 'Ready to start?')) { Pause-Exit }

# ---- 1. Find Skyrim install ----------------------------------------------
Step 'Looking for Skyrim Special Edition...'
$SkyrimRoot = $null
$vdf = "${env:ProgramFiles(x86)}\Steam\config\libraryfolders.vdf"
if (Test-Path $vdf) {
    $libs = Select-String -Path $vdf -Pattern '"path"\s+"([^"]+)"' |
        ForEach-Object { ($_.Matches[0].Groups[1].Value) -replace '\\\\', '\' }
    foreach ($lib in $libs) {
        $candidate = Join-Path $lib 'steamapps\common\Skyrim Special Edition'
        if (Test-Path (Join-Path $candidate 'SkyrimSE.exe')) {
            $SkyrimRoot = $candidate
            break
        }
    }
}
if (-not $SkyrimRoot) {
    Warn 'Could not auto-detect Skyrim. Please tell me where it is.'
    Write-Host '  (e.g. C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition)'
    $SkyrimRoot = Read-Host 'Skyrim folder'
    if (-not (Test-Path (Join-Path $SkyrimRoot 'SkyrimSE.exe'))) {
        Bad "SkyrimSE.exe not found in '$SkyrimRoot'."
        Pause-Exit 1
    }
}
$skyrimVer = (Get-Item (Join-Path $SkyrimRoot 'SkyrimSE.exe')).VersionInfo.FileVersion
Ok "Skyrim SE at $SkyrimRoot (version $skyrimVer)"

if ($skyrimVer -ne '1.6.1170.0') {
    Warn "This mod targets Skyrim 1.6.1170. You have $skyrimVer."
    Warn 'It MAY work but is not guaranteed.'
    if (-not (Ask 'Continue anyway?')) { Pause-Exit }
}

# ---- 2a. SKSE -------------------------------------------------------------
Step 'Checking SKSE64...'
$skseLoader = Join-Path $SkyrimRoot 'skse64_loader.exe'
if (-not (Test-Path $skseLoader)) {
    Warn 'SKSE64 not found.'
    Write-Host ''
    Write-Host 'You need to install SKSE64 2.2.6:' -ForegroundColor Yellow
    Write-Host '  1. Click "OK" below to open the SKSE download page'
    Write-Host '  2. Download "Current Anniversary Edition build 2.2.6"'
    Write-Host '  3. Open the .7z file (use 7-Zip if needed)'
    Write-Host '  4. Copy these files from the archive root into:'
    Write-Host "       $SkyrimRoot"
    Write-Host '         - skse64_loader.exe'
    Write-Host '         - skse64_1_6_1170.dll'
    Write-Host '         - any other skse64_*.dll files'
    Write-Host ''
    Write-Host '  5. Then re-run this installer.'
    Write-Host ''
    Read-Host 'Press Enter to open the SKSE download page'
    Start-Process 'https://skse.silverlock.org/'
    Pause-Exit 1
}
Ok 'SKSE64 installed'

# ---- 2b. Address Library --------------------------------------------------
Step 'Checking Address Library...'
$alFile = Join-Path $SkyrimRoot 'Data\SKSE\Plugins\versionlib-1-6-1170-0.bin'
if (-not (Test-Path $alFile)) {
    Warn 'Address Library not found.'
    Write-Host ''
    Write-Host 'You need Address Library for SKSE Plugins (free Nexus account required):' -ForegroundColor Yellow
    Write-Host '  1. Click "OK" below to open the Nexus page'
    Write-Host '  2. Sign in (free account works)'
    Write-Host '  3. Download "All in one (all game versions)"'
    Write-Host '  4. Open the .zip / .7z'
    Write-Host '  5. Inside, navigate to SKSE\Plugins\'
    Write-Host '  6. Copy every "versionlib-1-6-*.bin" file into:'
    Write-Host "       $(Join-Path $SkyrimRoot 'Data\SKSE\Plugins')"
    Write-Host ''
    Write-Host '  7. Then re-run this installer.'
    Write-Host ''
    Read-Host 'Press Enter to open the Nexus page'
    Start-Process 'https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files'
    Pause-Exit 1
}
Ok 'Address Library installed'

# ---- 2c. Tailscale --------------------------------------------------------
Step 'Checking Tailscale...'
$tailscaleExe = 'C:\Program Files\Tailscale\tailscale.exe'
if (-not (Test-Path $tailscaleExe)) {
    Warn 'Tailscale not installed.'
    Write-Host ''
    if (Ask 'Install Tailscale automatically via winget now?') {
        Write-Host 'Installing... (may take a minute)'
        try {
            winget install --id Tailscale.Tailscale -e --accept-package-agreements --accept-source-agreements | Out-Null
        } catch {
            Bad "winget install failed: $_"
            Write-Host 'Install manually from https://tailscale.com/download then re-run this installer.'
            Pause-Exit 1
        }
        if (-not (Test-Path $tailscaleExe)) {
            Bad 'Tailscale install completed but executable still missing.'
            Pause-Exit 1
        }
    } else {
        Write-Host 'Install Tailscale from https://tailscale.com/download then re-run.'
        Pause-Exit 1
    }
}
Ok 'Tailscale binary present'

# Status check
$tsStatus = & $tailscaleExe status 2>&1
if ($tsStatus -match 'Logged out|stopped|tailscaled is not running') {
    Warn 'Tailscale is installed but not signed in / not running.'
    Write-Host ''
    Write-Host 'Open the Tailscale tray icon (system tray, near the clock).'
    Write-Host "Sign in using the invite link $HostName sent you."
    Write-Host 'Wait for the icon to turn green.'
    Write-Host ''
    Read-Host 'Press Enter when Tailscale is signed in and connected'
    $tsStatus = & $tailscaleExe status 2>&1
    if ($tsStatus -match 'Logged out|stopped|tailscaled is not running') {
        Bad 'Tailscale still not connected. Try restarting Tailscale and re-running this installer.'
        Pause-Exit 1
    }
}
Ok 'Tailscale signed in'

# ---- 3. Install plugin ----------------------------------------------------
Step "Installing SkyrimReLive plugin..."
$pluginsDir = Join-Path $SkyrimRoot 'Data\SKSE\Plugins'
New-Item -ItemType Directory -Force -Path $pluginsDir | Out-Null
Copy-Item -Force (Join-Path $SrcDir 'SkyrimReLive.dll') $pluginsDir
$tomlDest = Join-Path $pluginsDir 'SkyrimReLive.toml'
if (Test-Path $tomlDest) {
    Warn "SkyrimReLive.toml already exists; not overwriting (you may have edited it)"
    Warn 'If you want defaults reset, delete it and re-run this installer.'
} else {
    Copy-Item -Force (Join-Path $SrcDir 'SkyrimReLive.toml') $pluginsDir
    Ok "Config installed at $tomlDest"
}
Ok "Plugin DLL installed at $pluginsDir"

# ---- 4. Save Skyrim path for LAUNCH.bat ----------------------------------
$pathFile = Join-Path $ScriptDir 'skyrim_path.txt'
$SkyrimRoot | Out-File -Encoding UTF8 -FilePath $pathFile -NoNewline
Ok "Saved Skyrim path so LAUNCH.bat knows where to go"

# ---- 5. Connectivity probe ------------------------------------------------
Step "Testing reachability to $HostName's server ($HostAddr`:$HostPort)..."
$client = $null
try {
    $client = New-Object System.Net.Sockets.UdpClient
    $client.Client.ReceiveTimeout = 2000
    $endpoint = New-Object System.Net.IPEndPoint(
        [System.Net.IPAddress]::Parse($HostAddr), $HostPort)
    $junk = [byte[]](0xFF, 0xFF, 0xFF, 0xFF)
    [void]$client.Send($junk, $junk.Length, $endpoint)
    Ok 'UDP packet sent successfully (server may not be running yet; that is OK).'
} catch {
    Warn "Could not send to $HostAddr`:$HostPort - $($_.Exception.Message)"
    Warn "Verify Tailscale tray icon is GREEN. Ask $HostName if his server PC is on."
} finally {
    if ($client) { $client.Close() }
}

# ---- Done -----------------------------------------------------------------
Banner 'Install complete!'
Write-Host 'How to play:'
Write-Host "  1. Make sure $HostName is running the SkyrimReLive server."
Write-Host '  2. Double-click LAUNCH.bat (in this folder) to start Skyrim.'
Write-Host '  3. Load any save. Wait for the world to load.'
Write-Host '  4. Press the tilde key (~) to open the in-game console.'
Write-Host '  5. Type:  rl status'
Write-Host '     You should see:  state=connected ...'
Write-Host ''
Write-Host 'Read README.txt for more info, console commands, and troubleshooting.'
Write-Host ''
Pause-Exit
