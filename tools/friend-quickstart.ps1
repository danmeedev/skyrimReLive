# Friend quickstart. Single-command setup for a friend who has cloned
# the SkyrimReLive repo and wants to join the host's game.
#
# Detects what's available and picks the right path:
#   1. Pre-built friend zip in dist/  → install from it
#   2. Latest GitHub release           → download + install
#   3. Local source + build tools      → build + install
#
# Usage:
#   .\tools\friend-quickstart.ps1 -HostIp 100.x.y.z -HostName "danme"
#   .\tools\friend-quickstart.ps1 -ZipPath C:\path\to\bundle.zip
#   .\tools\friend-quickstart.ps1 -ForceBuild
#   .\tools\friend-quickstart.ps1 -ForceDownload

[CmdletBinding()]
param(
    [Parameter(HelpMessage='Host Tailscale IP (required for build/download paths)')]
    [string]$HostIp = '',

    [Parameter(HelpMessage="Friendly name for the host shown in installer prompts")]
    [string]$HostName = 'the host',

    [Parameter(HelpMessage='Use a specific local zip instead of building/downloading')]
    [string]$ZipPath = '',

    [Parameter(HelpMessage='Skip the auto-pick logic and force building from source')]
    [switch]$ForceBuild,

    [Parameter(HelpMessage='Skip the auto-pick logic and force downloading from GitHub Releases')]
    [switch]$ForceDownload,

    [Parameter(HelpMessage='GitHub repo slug for releases (owner/repo)')]
    [string]$Repo = 'danmeedev/skyrimReLive'
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot = Split-Path -Parent $ScriptDir
$DistDir = Join-Path $RepoRoot 'dist'
$BundleDir = Join-Path $DistDir 'SkyrimReLive-friend'

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
function Fail($msg) { Write-Host "    [FAIL] $msg" -ForegroundColor Red; exit 1 }
function Ask($q)    { ($a = Read-Host "$q [Y/n]"); return ($a -eq '' -or $a -match '^[Yy]') }

Banner 'SkyrimReLive - friend quickstart'

# ---- 1. Resolve host info ------------------------------------------------
if (-not $HostIp -and -not $ZipPath) {
    Write-Host "I need your friend's Tailscale IP to configure the plugin."
    Write-Host '  (Looks like: 100.x.y.z — they got it from `tailscale ip -4` on their PC.)'
    $HostIp = Read-Host 'Host Tailscale IP'
    if (-not $HostIp -or $HostIp -notmatch '^\d+\.\d+\.\d+\.\d+$') {
        Fail "That doesn't look like a valid IP."
    }
}
if (-not $ZipPath -and $HostName -eq 'the host') {
    $h = Read-Host "Friend's name (for nicer messages, e.g. 'danme') [the host]"
    if ($h) { $HostName = $h }
}

# ---- 2. Decide which path to take ---------------------------------------
$zipDest = Join-Path $DistDir 'SkyrimReLive-friend.zip'
$mode = $null
if ($ZipPath) {
    if (-not (Test-Path $ZipPath)) { Fail "Zip not found: $ZipPath" }
    $mode = 'zip'
    Ok "Using local zip: $ZipPath"
} elseif ($ForceBuild) {
    $mode = 'build'
} elseif ($ForceDownload) {
    $mode = 'download'
} else {
    Step 'Picking the easiest path that should work...'
    # Prefer download (no toolchain needed) if no other override
    $mode = 'download'
}

# ---- 3a. Download path --------------------------------------------------
if ($mode -eq 'download') {
    Step "Fetching latest release from github.com/$Repo"
    $api = "https://api.github.com/repos/$Repo/releases/latest"
    $assetUrl = $null
    try {
        $rel = Invoke-RestMethod -Uri $api -UseBasicParsing -Headers @{
            'User-Agent' = 'SkyrimReLive-friend-quickstart'
        }
        $asset = $rel.assets | Where-Object { $_.name -eq 'SkyrimReLive-friend.zip' } | Select-Object -First 1
        if ($asset) { $assetUrl = $asset.browser_download_url }
    } catch {
        Warn "GitHub API call failed: $($_.Exception.Message)"
    }

    if (-not $assetUrl) {
        Warn 'No SkyrimReLive-friend.zip found in the latest release.'
        Write-Host ''
        Write-Host 'You have three options:' -ForegroundColor Yellow
        Write-Host "  1. Ask $HostName to publish a release with SkyrimReLive-friend.zip"
        Write-Host '     (https://github.com/' + $Repo + '/releases/new)'
        Write-Host "  2. Ask $HostName to send you the zip directly. Then re-run with:"
        Write-Host "       .\tools\friend-quickstart.ps1 -ZipPath C:\path\to\bundle.zip"
        Write-Host '  3. Build from source (needs Visual Studio + Rust):'
        Write-Host "       .\tools\friend-quickstart.ps1 -ForceBuild"
        Write-Host ''
        if (Ask 'Try building from source instead?') {
            $mode = 'build'
        } else {
            exit 1
        }
    } else {
        New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
        Step "Downloading $assetUrl"
        Invoke-WebRequest -Uri $assetUrl -OutFile $zipDest -UseBasicParsing
        Ok "Downloaded to $zipDest ($((Get-Item $zipDest).Length) bytes)"
        $ZipPath = $zipDest
        $mode = 'zip'
    }
}

# ---- 3b. Build path -----------------------------------------------------
if ($mode -eq 'build') {
    Step 'Checking build prerequisites...'
    $vsOk = $false
    $vsCandidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\Community',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools',
        'C:\Program Files\Microsoft Visual Studio\2026\Community'
    )
    foreach ($c in $vsCandidates) { if (Test-Path $c) { $vsOk = $true; break } }
    if (-not $vsOk) {
        Fail @'
Visual Studio with the C++ desktop workload not found.
Install it from https://visualstudio.microsoft.com/downloads/
(pick "Visual Studio Community" — free — and select the
"Desktop development with C++" workload during install).
Then re-run this script.
'@
    }
    Ok 'Visual Studio detected'

    if (-not (Get-Command cargo -ErrorAction SilentlyContinue) -and -not (Test-Path "$env:USERPROFILE\.cargo\bin\cargo.exe")) {
        Warn 'Rust not installed.'
        if (Ask 'Install Rust via winget now?') {
            winget install --id Rustlang.Rustup -e --accept-package-agreements --accept-source-agreements
        } else {
            Fail 'Rust required for the build pipeline. Install from https://rustup.rs/'
        }
    }
    Ok 'Rust available'

    Step 'Running setup.ps1 to bootstrap vcpkg + build the plugin'
    Write-Host '(this can take 10-15 minutes the first time, mostly compiling deps)'
    & (Join-Path $ScriptDir 'setup.ps1') -SkipSkse
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) { Fail 'setup.ps1 failed' }

    Step "Building friend bundle for $HostName ($HostIp)"
    & (Join-Path $ScriptDir 'build-friend-bundle.ps1') -HostIp $HostIp -HostName $HostName -NoBuild
    if ($LASTEXITCODE -ne 0 -and $null -ne $LASTEXITCODE) { Fail 'build-friend-bundle.ps1 failed' }
    $ZipPath = $zipDest
    $mode = 'zip'
}

# ---- 4. Extract zip and run the bundled installer ----------------------
if ($mode -ne 'zip') { Fail "Unexpected mode: $mode" }

if (Test-Path $BundleDir) { Remove-Item -Recurse -Force $BundleDir }
New-Item -ItemType Directory -Force -Path $BundleDir | Out-Null
Step "Extracting $ZipPath to $BundleDir"
Expand-Archive -Path $ZipPath -DestinationPath $BundleDir -Force

$installBat = Join-Path $BundleDir 'INSTALL.bat'
if (-not (Test-Path $installBat)) { Fail "INSTALL.bat not found in extracted bundle" }

Banner 'Bundle ready - running the installer now'
Write-Host "It will check for SKSE, Address Library, and Tailscale, install the"
Write-Host "plugin, and probe $HostName's server."
Write-Host ''
Write-Host "Press Enter to start the installer..."
Read-Host

& cmd /c "`"$installBat`""

Banner 'Quickstart finished'
Write-Host "Bundle saved at:  $BundleDir"
Write-Host "When you want to play: double-click LAUNCH.bat in that folder."
Write-Host "Or shortcut it to your desktop."
Write-Host ''
