# Launch each leg of a DLC cache comparison with the same game and profile.
# -RefreshCache forces a cache miss and rebuild without deleting the player's cache.
# The guest songcache and other profile data are deliberately shared.
# Examples:
#   .\scripts\boot_dlc_cache_compare.ps1 -Cache On
#   .\scripts\boot_dlc_cache_compare.ps1 -Cache Off
#   .\scripts\boot_dlc_cache_compare.ps1 -Cache On -RefreshCache -SafeGraphics
#   .\scripts\boot_dlc_cache_compare.ps1 -Cache On -DlcLibrary 'D:\Songs'
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('On', 'Off')]
    [string]$Cache,
    [string]$BuildDir = 'out/build/win-amd64-release',
    [string]$GameRoot = 'game',
    [string]$DlcLibrary = '',
    [string]$UserDataRoot = '',
    [string]$LauncherProfile = '',
    [switch]$RefreshCache,
    [switch]$SafeGraphics
)

$ErrorActionPreference = 'Stop'
if ($RefreshCache -and $Cache -ne 'On') {
    throw '-RefreshCache requires -Cache On.'
}
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildPath = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir
} else { Join-Path $projectRoot $BuildDir }))
$gamePath = [System.IO.Path]::GetFullPath($(if ([System.IO.Path]::IsPathRooted($GameRoot)) {
    $GameRoot
} else { Join-Path $projectRoot $GameRoot }))
$exe = Join-Path $buildPath 'rb_blitz.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Build not found: $exe" }
if (-not (Test-Path -LiteralPath $gamePath -PathType Container)) { throw "Game root not found: $gamePath" }

$enabled = if ($Cache -eq 'On') { 'true' } else { 'false' }
$refresh = if ($RefreshCache) { 'true' } else { 'false' }
$launchArgs = @(
    "--game_data_root=`"$gamePath`"",
    "--enhancements_dlc_cache=$enabled",
    "--refresh_dlc_cache=$refresh"
)
if ($SafeGraphics) {
    # OnPreSetup forces D3D12 when safe graphics is selected. Use this switch
    # on both legs of a comparison; --gpu_backend=vulkan cannot override it.
    $launchArgs += '--settings_safe_graphics=true'
}
if ($DlcLibrary) {
    $libraryPaths = @()
    foreach ($entry in ($DlcLibrary -split ';')) {
        $libraryPath = [System.IO.Path]::GetFullPath($entry)
        if (-not (Test-Path -LiteralPath $libraryPath -PathType Container)) {
            throw "DLC library not found: $libraryPath"
        }
        $libraryPaths += $libraryPath
    }
    $launchArgs += "--dlc_library=`"$($libraryPaths -join ';')`""
}
if ($UserDataRoot) {
    $profilePath = [System.IO.Path]::GetFullPath($UserDataRoot)
    if (-not (Test-Path -LiteralPath $profilePath -PathType Container)) {
        throw "User data root not found: $profilePath"
    }
    $launchArgs += "--user_data_root=`"$profilePath`""
    if (-not $LauncherProfile) { $LauncherProfile = Join-Path $profilePath 'launcher.toml' }
}
if ($LauncherProfile) {
    $launchArgs += "--launcher_profile=`"$([System.IO.Path]::GetFullPath($LauncherProfile))`""
}

Write-Host "DLC cache comparison: $Cache; forced refresh: $RefreshCache"
Write-Host "Executable: $exe"
Write-Host "Game root: $gamePath"
if ($SafeGraphics) { Write-Host 'Safe graphics: D3D12, resolution scale 1.' }
Write-Host 'Keep the same -DlcLibrary, -UserDataRoot, and -LauncherProfile values for both legs.'
Write-Host "After reaching the main menu, this script prints the timing lines from $buildPath\logs."
$startedAt = Get-Date
$game = Start-Process -FilePath $exe -WorkingDirectory $buildPath -ArgumentList $launchArgs -PassThru
$game.WaitForExit()
$latestLog = Get-ChildItem -LiteralPath (Join-Path $buildPath 'logs') -Filter '*.log' -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -ge $startedAt } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($latestLog) { Write-Host "Latest log: $($latestLog.FullName)" }
if ($latestLog) {
    Select-String -LiteralPath $latestLog.FullName -Pattern @(
        'settings_boot:   gpu_backend = ',
        'settings_boot: resolution_scale = ',
        "settings_boot: 'gpu_backend'.*did not stand",
        "Vulkan device '",
        'Direct3D 12 device and OS features:',
        'dlc_cache: when=this boot',
        'dlc_cache: end when=this boot',
        'graphics_loading: attempt 1 released',
        'dlc_load: end'
    ) | ForEach-Object { Write-Host $_.Line }
}
if ($game.ExitCode -ne 0) {
    $completed = $latestLog -and (Select-String -LiteralPath $latestLog.FullName -Pattern "dlc_load: end attempt=\d+ cache=$enabled\b" -Quiet)
    if ($completed) {
        Write-Warning "Game exited with code $($game.ExitCode) after DLC loading completed; this run's timing is usable."
    } else {
        throw "Game exited with code $($game.ExitCode) before DLC loading completed."
    }
}
