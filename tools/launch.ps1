# SkyrimReLive launch helper.
# - (Optionally) rebuilds the plugin if sources changed
# - Re-deploys SkyrimReLive.dll to Skyrim's Data\SKSE\Plugins
# - Starts the Rust server in a new console window
# - Launches skse64_loader.exe
#
# When this script exits (Ctrl+C in this window, or after Skyrim closes),
# the server window is closed too.

[CmdletBinding()]
param(
    [string]$SkyrimRoot = 'C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition',
    [string]$VcpkgRoot  = 'C:\vcpkg',
    [switch]$NoRebuild,
    [switch]$NoServer,
    [switch]$WaitForGame  # if set, block until Skyrim exits and then stop server
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Ok($msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Warn($msg) { Write-Host "    $msg" -ForegroundColor Yellow }
function Fail($msg) { Write-Host "    $msg" -ForegroundColor Red; exit 1 }

$skseLoader = Join-Path $SkyrimRoot 'skse64_loader.exe'
if (-not (Test-Path $skseLoader)) {
    Fail "skse64_loader.exe not in $SkyrimRoot. Run tools\setup.ps1 first."
}

# Bail if Skyrim or the SKSE loader is already running. Redeploy can't
# overwrite a locked DLL, and even if it could, Skyrim has the old DLL
# memory-mapped — restart is the only way to pick up a new build.
$running = Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -in @('SkyrimSE','skse64_loader') }
if ($running) {
    $names = ($running | ForEach-Object { "$($_.ProcessName) (PID $($_.Id))" }) -join ', '
    Fail "Skyrim is already running: $names. Close it first, then re-run launch.ps1."
}

# --- Build & deploy plugin (incremental) -------------------------------------

if (-not $NoRebuild) {
    $cmakeCandidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    )
    $cmake = $cmakeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $cmake) { Fail 'CMake not found. Run setup.ps1.' }

    $env:VCPKG_ROOT = $VcpkgRoot
    Step 'Building plugin (incremental)'
    Push-Location (Join-Path $RepoRoot 'client\plugin')
    try {
        if (-not (Test-Path 'build')) {
            & $cmake -S . -B build `
                -G 'Visual Studio 18 2026' -A x64 -T v143 `
                "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot/scripts/buildsystems/vcpkg.cmake" `
                "-DVCPKG_TARGET_TRIPLET=x64-windows-static-md" `
                "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
            if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed" }
        }
        & $cmake --build build --config Release
        if ($LASTEXITCODE -ne 0) { Fail "cmake build failed" }
    } finally {
        Pop-Location
    }

    $pluginDll = Join-Path $RepoRoot 'client\plugin\build\Release\SkyrimReLive.dll'
    $pluginDir = Join-Path $SkyrimRoot 'Data\SKSE\Plugins'
    $null = New-Item -ItemType Directory -Force -Path $pluginDir
    Copy-Item -Force $pluginDll -Destination $pluginDir
    Ok "Deployed plugin to $pluginDir"

    # Don't overwrite the user's SkyrimReLive.toml — only seed it if missing.
    $cfgSrc = Join-Path $RepoRoot 'client\plugin\SkyrimReLive.toml'
    $cfgDst = Join-Path $pluginDir 'SkyrimReLive.toml'
    if (-not (Test-Path $cfgDst)) {
        Copy-Item -Force $cfgSrc -Destination $cfgDst
        Ok "Seeded default config at $cfgDst"
    }
}

# --- Start server in a new console window ------------------------------------

$serverProc = $null
if (-not $NoServer) {
    $cargoExe = "$env:USERPROFILE\.cargo\bin\cargo.exe"
    if (-not (Test-Path $cargoExe)) {
        $c = Get-Command cargo -ErrorAction SilentlyContinue
        if ($c) { $cargoExe = $c.Source } else { Fail 'cargo not found' }
    }
    $serverDir = Join-Path $RepoRoot 'server'
    $serverBin = Join-Path $RepoRoot 'target\release\skyrim-relive-server.exe'

    # Prebuild in this shell so we block until the binary exists, THEN launch
    # it in a new window. `cargo run` in a child process made the window open
    # instantly but the server didn't bind for 30–60 s on cold cache, and
    # Skyrim's kDataLoaded fires in ~8 s — hence the old race.
    Step 'Building server (release, blocks until binary exists)'
    Push-Location $serverDir
    try {
        & $cargoExe build --release
        if ($LASTEXITCODE -ne 0) { Fail "cargo build failed" }
    } finally {
        Pop-Location
    }
    if (-not (Test-Path $serverBin)) { Fail "server binary missing at $serverBin" }

    Step 'Starting server (new console window)'
    $serverProc = Start-Process -PassThru -FilePath $serverBin `
        -WorkingDirectory $serverDir
    Ok "Server PID $($serverProc.Id) (close that window or Ctrl+C in it to stop)"

    # Binary starts near-instantly, but wait briefly for the UDP bind to finish.
    Start-Sleep -Seconds 1
    if ($serverProc.HasExited) {
        Fail "server process exited immediately (exit code $($serverProc.ExitCode)); check its window"
    }
}

# --- Launch SKSE loader ------------------------------------------------------

Step "Launching $skseLoader"
$gameProc = Start-Process -PassThru -FilePath $skseLoader -WorkingDirectory $SkyrimRoot

if ($WaitForGame) {
    Step 'Waiting for Skyrim to exit (Ctrl+C to abort)'
    try {
        $gameProc.WaitForExit()
    } finally {
        if ($serverProc -and -not $serverProc.HasExited) {
            Step 'Stopping server'
            try { $serverProc.Kill() } catch {}
        }
    }
} else {
    Ok 'Skyrim launched. Server keeps running in its own window.'
    Write-Host ''
    Write-Host 'After loading a save, check:' -ForegroundColor Cyan
    Write-Host "  $env:USERPROFILE\Documents\My Games\Skyrim Special Edition\SKSE\SkyrimReLive.log"
    Write-Host '  (should contain: "server replied: WORLD dovahkiin <ts>")'
}
