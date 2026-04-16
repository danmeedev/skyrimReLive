# Build a one-click friend-install bundle from the templates in
# tools/friend-bundle/, substituting {{HOST_IP}} and {{HOST_NAME}}.
#
# Usage:
#   .\tools\build-friend-bundle.ps1                          # auto-detect host IP
#   .\tools\build-friend-bundle.ps1 -HostIp 100.x.y.z
#   .\tools\build-friend-bundle.ps1 -HostName "danme"
#   .\tools\build-friend-bundle.ps1 -NoBuild                 # skip cmake build
#
# Output: dist/SkyrimReLive-friend.zip + the staged folder dist/SkyrimReLive-friend/.

[CmdletBinding()]
param(
    [string]$HostIp = '',
    [string]$HostName = 'the host',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot = Split-Path -Parent $ScriptDir

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "    [OK] $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "    [WARN] $msg" -ForegroundColor Yellow }
function Fail($msg) { Write-Host "    [FAIL] $msg" -ForegroundColor Red; exit 1 }

# ---- 1. Resolve host IP --------------------------------------------------
if (-not $HostIp) {
    Step 'Auto-detecting Tailscale IP...'
    $tailscaleExe = 'C:\Program Files\Tailscale\tailscale.exe'
    if (Test-Path $tailscaleExe) {
        $detected = & $tailscaleExe ip -4 2>&1 | Select-Object -First 1
        if ($detected -and $detected -match '^\d+\.\d+\.\d+\.\d+$') {
            $HostIp = $detected.Trim()
            Ok "Detected: $HostIp"
        } else {
            Fail "Could not parse Tailscale IP from output: $detected"
        }
    } else {
        Fail 'Tailscale not installed and no -HostIp given. Pass -HostIp <addr>.'
    }
}
Ok "Host IP:    $HostIp"
Ok "Host name:  $HostName"

# ---- 2. Build the plugin (Release) ---------------------------------------
$dll = Join-Path $RepoRoot 'client\plugin\build\Release\SkyrimReLive.dll'
if (-not $NoBuild) {
    Step 'Building plugin (Release)'
    $cmakeCandidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    )
    $cmake = $cmakeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $cmake) {
        $c = Get-Command cmake -ErrorAction SilentlyContinue
        if ($c) { $cmake = $c.Source } else { Fail 'CMake not found' }
    }
    & $cmake --build (Join-Path $RepoRoot 'client\plugin\build') --config Release
    if ($LASTEXITCODE -ne 0) { Fail "Plugin build failed (exit $LASTEXITCODE)" }
}
if (-not (Test-Path $dll)) {
    Fail "Plugin DLL missing at $dll. Run setup.ps1 first or drop -NoBuild."
}
Ok "Plugin DLL: $dll ($((Get-Item $dll).Length) bytes)"

# ---- 3. Stage bundle dir from templates ----------------------------------
$tmplRoot = Join-Path $ScriptDir 'friend-bundle'
$distRoot = Join-Path $RepoRoot 'dist'
$bundleDir = Join-Path $distRoot 'SkyrimReLive-friend'

if (-not (Test-Path $tmplRoot)) { Fail "Templates missing: $tmplRoot" }

if (Test-Path $bundleDir) { Remove-Item -Recurse -Force $bundleDir }
New-Item -ItemType Directory -Force -Path $bundleDir | Out-Null
Step "Staging templates to $bundleDir"
Copy-Item -Recurse -Force (Join-Path $tmplRoot '*') $bundleDir

# ---- 4. Substitute placeholders + drop .template suffix -----------------
Step 'Substituting placeholders ({{HOST_IP}}, {{HOST_NAME}})'
Get-ChildItem -Recurse -File -Path $bundleDir | ForEach-Object {
    # Skip binary files (DLL, etc.) by extension
    if ($_.Extension -in '.dll', '.exe', '.bin', '.zip') { return }
    $content = Get-Content -Raw -Encoding UTF8 $_.FullName
    if ($content -match '\{\{HOST_IP\}\}|\{\{HOST_NAME\}\}') {
        $new = $content -replace '\{\{HOST_IP\}\}', $HostIp `
                        -replace '\{\{HOST_NAME\}\}', $HostName
        Set-Content -NoNewline -Encoding UTF8 -Path $_.FullName -Value $new
    }
}
# Rename *.template files (drop the .template suffix)
Get-ChildItem -Recurse -File -Path $bundleDir -Filter '*.template' | ForEach-Object {
    $newName = $_.FullName -replace '\.template$', ''
    Move-Item -Force $_.FullName $newName
}
Ok 'Template substitutions applied'

# ---- 5. Drop in the freshly-built DLL ------------------------------------
Step 'Copying plugin DLL into bundle'
Copy-Item -Force $dll (Join-Path $bundleDir 'files\SkyrimReLive.dll')
Ok 'DLL bundled'

# ---- 6. Zip ---------------------------------------------------------------
$zipPath = Join-Path $distRoot 'SkyrimReLive-friend.zip'
if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
Step "Compressing to $zipPath"
Compress-Archive -Path (Join-Path $bundleDir '*') -DestinationPath $zipPath -Force
$zipSize = (Get-Item $zipPath).Length

# ---- 7. Summary -----------------------------------------------------------
Write-Host ''
Write-Host '================================================' -ForegroundColor Green
Write-Host '  Friend bundle ready' -ForegroundColor Green
Write-Host '================================================' -ForegroundColor Green
Write-Host "  Zip:    $zipPath ($zipSize bytes)"
Write-Host "  Folder: $bundleDir"
Write-Host "  Host:   $HostName @ $HostIp"
Write-Host ''
Write-Host 'Send the zip to your friend(s). They double-click INSTALL.bat to install.'
