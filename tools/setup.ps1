# SkyrimReLive one-time setup.
# - Verifies prereqs (Rust, CMake, vcpkg, 7-Zip)
# - Downloads SKSE64 if not already installed
# - Builds the plugin
# - Deploys SkyrimReLive.dll to Skyrim's Data\SKSE\Plugins
#
# Idempotent: re-running is safe and skips work that's already done.

[CmdletBinding()]
param(
    [string]$SkyrimRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition',
    [string]$VcpkgRoot  = 'C:\vcpkg',
    [string]$SkseUrl    = 'https://skse.silverlock.org/beta/skse64_2_02_06.7z',
    [string]$SkseSha256 = '',  # optional pin; left blank = trust download
    [switch]$SkipSkse,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "    $msg" -ForegroundColor Yellow }
function Fail($msg) { Write-Host "    $msg" -ForegroundColor Red; exit 1 }

# --- Prereq checks -----------------------------------------------------------

Step 'Checking prerequisites'

if (-not (Test-Path $SkyrimRoot)) {
    Fail "Skyrim SE not found at $SkyrimRoot. Pass -SkyrimRoot <path> if it's elsewhere."
}
$skyrimExe = Join-Path $SkyrimRoot 'SkyrimSE.exe'
if (-not (Test-Path $skyrimExe)) {
    Fail "SkyrimSE.exe missing under $SkyrimRoot. Reinstall Skyrim SE via Steam."
}
$skyrimVer = (Get-Item $skyrimExe).VersionInfo.FileVersion
Ok "Skyrim SE found ($skyrimVer)"

# rust / cargo
$cargo = Get-Command cargo -ErrorAction SilentlyContinue
if (-not $cargo) {
    $cargo = Get-Command "$env:USERPROFILE\.cargo\bin\cargo.exe" -ErrorAction SilentlyContinue
}
if (-not $cargo) {
    Fail 'cargo not found. Install Rust: winget install Rustlang.Rustup'
}
Ok "Rust: $($cargo.Source)"

# cmake (prefer VS 2026 bundled cmake which knows the VS 18 generator)
$cmakeCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\CMake\bin\cmake.exe'
)
$cmake = $cmakeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $cmake) {
    $c = Get-Command cmake -ErrorAction SilentlyContinue
    if ($c) { $cmake = $c.Source }
}
if (-not $cmake) {
    Fail 'CMake not found. Install Visual Studio 2026/2022 with C++ workload (bundles cmake).'
}
Ok "CMake: $cmake"

# vcpkg
if (-not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
    Step "Bootstrapping vcpkg at $VcpkgRoot"
    if (-not (Test-Path $VcpkgRoot)) {
        git clone --depth 1 https://github.com/microsoft/vcpkg.git $VcpkgRoot
    }
    & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if (-not (Test-Path (Join-Path $VcpkgRoot 'vcpkg.exe'))) {
        Fail "vcpkg bootstrap failed."
    }
}
$env:VCPKG_ROOT = $VcpkgRoot
Ok "vcpkg: $VcpkgRoot"

# 7-Zip (only needed for SKSE archive extraction)
$sevenZip = $null
if (-not $SkipSkse) {
    $sevenZipCandidates = @(
        'C:\Program Files\7-Zip\7z.exe',
        'C:\Program Files (x86)\7-Zip\7z.exe'
    )
    $sevenZip = $sevenZipCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $sevenZip) {
        Step 'Installing 7-Zip via winget (needed to extract SKSE archive)'
        winget install --id 7zip.7zip -e --accept-package-agreements --accept-source-agreements | Out-Null
        $sevenZip = $sevenZipCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        if (-not $sevenZip) { Fail '7-Zip install failed. Install manually then re-run.' }
    }
    Ok "7-Zip: $sevenZip"
}

# --- SKSE install ------------------------------------------------------------

if (-not $SkipSkse) {
    $skseLoader = Join-Path $SkyrimRoot 'skse64_loader.exe'
    if (Test-Path $skseLoader) {
        Ok "SKSE64 already installed (skse64_loader.exe present)"
    } else {
        Step "Downloading SKSE64 from $SkseUrl"
        $tmpDir = Join-Path $env:TEMP "skyrimrelive-skse"
        $null = New-Item -ItemType Directory -Force -Path $tmpDir
        $archive = Join-Path $tmpDir 'skse64.7z'
        Invoke-WebRequest -Uri $SkseUrl -OutFile $archive
        if ($SkseSha256) {
            $h = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLower()
            if ($h -ne $SkseSha256.ToLower()) { Fail "SKSE checksum mismatch: got $h" }
        }

        Step 'Extracting SKSE64'
        & $sevenZip x -y -o"$tmpDir" $archive | Out-Null
        $extracted = Get-ChildItem -Directory -Path $tmpDir | Where-Object { $_.Name -like 'skse64_*' } | Select-Object -First 1
        if (-not $extracted) { Fail "SKSE archive layout unexpected; nothing in $tmpDir" }

        Step "Installing SKSE64 to $SkyrimRoot"
        # Loader + core DLLs at archive root → Skyrim install dir
        Get-ChildItem -File -Path $extracted.FullName | ForEach-Object {
            Copy-Item -Force $_.FullName -Destination $SkyrimRoot
        }
        # Papyrus scripts & sources → Data\
        $extData = Join-Path $extracted.FullName 'Data'
        if (Test-Path $extData) {
            Copy-Item -Recurse -Force (Join-Path $extData '*') -Destination (Join-Path $SkyrimRoot 'Data')
        }
        Ok 'SKSE64 installed'
    }
}

# --- Plugin build ------------------------------------------------------------

if (-not $SkipBuild) {
    Step 'Configuring plugin (cmake)'
    Push-Location (Join-Path $RepoRoot 'client\plugin')
    try {
        & $cmake -S . -B build `
            -G 'Visual Studio 18 2026' -A x64 -T v143 `
            "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
            "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md" `
            "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
        if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed (exit $LASTEXITCODE)" }

        Step 'Building plugin (cmake --build)'
        & $cmake --build build --config Release
        if ($LASTEXITCODE -ne 0) { Fail "cmake build failed (exit $LASTEXITCODE)" }
    } finally {
        Pop-Location
    }
    Ok 'Plugin built'

    # --- Deploy --------------------------------------------------------------
    $pluginDll = Join-Path $RepoRoot 'client\plugin\build\Release\SkyrimReLive.dll'
    if (-not (Test-Path $pluginDll)) { Fail "Build succeeded but DLL missing at $pluginDll" }
    $pluginDir = Join-Path $SkyrimRoot 'Data\SKSE\Plugins'
    $null = New-Item -ItemType Directory -Force -Path $pluginDir
    Copy-Item -Force $pluginDll -Destination $pluginDir
    Ok "Deployed DLL to $pluginDir"

    # Deploy the default config only if the user doesn't already have one —
    # they may have edited server_host for a LAN/WAN server.
    $cfgSrc = Join-Path $RepoRoot 'client\plugin\SkyrimReLive.toml'
    $cfgDst = Join-Path $pluginDir 'SkyrimReLive.toml'
    if (Test-Path $cfgDst) {
        Ok "Config already present at $cfgDst (not overwritten)"
    } else {
        Copy-Item -Force $cfgSrc -Destination $cfgDst
        Ok "Deployed default config to $cfgDst"
    }
}

# --- Address Library check ---------------------------------------------------
# CommonLibSSE-NG's SKSE::Init unconditionally calls REL::IDDatabase::get(),
# which fails ("failed to open address library") if AL .bin files aren't present.
# AL is hosted on Nexus and requires a free account, so we can't auto-download
# — just detect and explain.
# For 1.6.x the convention is `versionlib-1-6-NNNN-0.bin`; for 1.5.x it's
# `version-1-5-NN-0.bin`. The "All in one (all game versions)" download from
# Nexus has both; we just need the one matching the running runtime.
$pluginsDir = Join-Path $SkyrimRoot 'Data\SKSE\Plugins'
$verParts = $skyrimVer.Split('.')
$alStem = if ([int]$verParts[1] -ge 6) {
    "versionlib-$($verParts[0])-$($verParts[1])-$($verParts[2])-$($verParts[3]).bin"
} else {
    "version-$($verParts[0])-$($verParts[1])-$($verParts[2])-$($verParts[3]).bin"
}
$alFile = Join-Path $pluginsDir $alStem
if (-not (Test-Path $alFile)) {
    Write-Host ''
    Warn 'Address Library is NOT installed.'
    Warn "Missing: $alFile"
    Write-Host ''
    Write-Host '  Download "All in one (all game versions)" from:' -ForegroundColor Yellow
    Write-Host '    https://www.nexusmods.com/skyrimspecialedition/mods/32444?tab=files' -ForegroundColor Yellow
    Write-Host '  Extract and copy SKSE/Plugins/*.bin into:' -ForegroundColor Yellow
    Write-Host "    $pluginsDir" -ForegroundColor Yellow
    Write-Host ''
} else {
    Ok "Address Library present ($alStem)"
}

Step 'Setup complete'
Write-Host ''
Write-Host 'Next:' -ForegroundColor Cyan
Write-Host "  tools\launch.ps1     # starts the server and SKSE loader"
