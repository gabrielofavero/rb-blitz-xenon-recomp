<#
.SYNOPSIS
  Launches the built game for the owner's P1 texture probe and preserves its log.

.EXAMPLE
  .\scripts\acceptance\acceptance_button_schemes_p1.ps1
  .\scripts\acceptance\acceptance_button_schemes_p1.ps1 -ButtonHash 0123456789ABCDEF -PadHash FEDCBA9876543210
  .\scripts\acceptance\acceptance_button_schemes_p1.ps1 -ButtonHash 0123456789ABCDEF -PadHash FEDCBA9876543210 -Switchable -MeasureFrames
#>
[CmdletBinding()]
param(
    [string]$ButtonHash,
    [string]$PadHash,
    [ValidateSet('d3d12', 'vulkan')][string]$Backend = 'd3d12',
    [switch]$Switchable,
    [switch]$MeasureFrames
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $repo 'out/build/win-amd64-release'
$exe = Join-Path $build 'rb_blitz.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Build the game first: cmake --build --preset win-amd64-release"
}
foreach ($hash in @($ButtonHash, $PadHash)) {
    if ($hash -and $hash -notmatch '^(0x)?[0-9a-fA-F]{1,16}$') {
        throw "Expected a hexadecimal 64-bit hash, got '$hash'"
    }
}
$evidence = Join-Path $repo 'out/button-schemes/p1'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
$emptyDlc = Join-Path $evidence 'empty-dlc'
New-Item -ItemType Directory -Force -Path $emptyDlc | Out-Null
$switchFile = Join-Path $evidence 'markers-off.flag'
$oldTrace = $env:RB_BLITZ_P1_TRACE
$oldButton = $env:RB_BLITZ_P1_BUTTON_HASH
$oldPad = $env:RB_BLITZ_P1_PAD_HASH
$oldSwitch = $env:RB_BLITZ_P1_SWITCH_FILE
$env:RB_BLITZ_P1_TRACE = '1'
$env:RB_BLITZ_P1_BUTTON_HASH = $ButtonHash
$env:RB_BLITZ_P1_PAD_HASH = $PadHash
if ($Switchable) {
    Remove-Item -LiteralPath $switchFile -Force -ErrorAction SilentlyContinue
    $env:RB_BLITZ_P1_SWITCH_FILE = $switchFile
    Write-Host "While the game is open, create $switchFile to show retail art; remove it to restore markers."
} else {
    Remove-Item Env:RB_BLITZ_P1_SWITCH_FILE -ErrorAction SilentlyContinue
}
Write-Host "P1 $Backend probe (DLC disabled). Visit the menu prompt band, then Help & Options > Controls > Gameplay Mapping > Controller > Controller Presets. Close the game afterward."
Write-Host 'The game window is yours to control; this script only starts it and copies its log.'
$launchArgs = @('--game_data_root=..\..\..\game', "--gpu_backend=$Backend",
    "--dlc_root=`"$emptyDlc`"", "--dlc_library=`"$emptyDlc`"")
if ($MeasureFrames) {
    $launchArgs += @('--log_noisy=true', '--log_level=trace', '--log_flush_interval=1',
        '--log_max_file_size_mb=200')
    Write-Host 'Frame tracing enabled: the log will include VdSwap and XE_SWAP frame intervals.'
}
$started = Get-Date
try {
    $process = Start-Process -FilePath $exe -ArgumentList $launchArgs `
        -WorkingDirectory $build -WindowStyle Normal -PassThru -Wait
} finally {
    $env:RB_BLITZ_P1_TRACE = $oldTrace
    $env:RB_BLITZ_P1_BUTTON_HASH = $oldButton
    $env:RB_BLITZ_P1_PAD_HASH = $oldPad
    $env:RB_BLITZ_P1_SWITCH_FILE = $oldSwitch
}
$latest = Get-ChildItem -LiteralPath (Join-Path $build 'logs') -Filter '*.log' -File |
    Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-5) } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($latest) {
    $destination = Join-Path $evidence ("$Backend-" + $started.ToString('yyyyMMdd-HHmmss') + '.log')
    Copy-Item -LiteralPath $latest.FullName -Destination $destination
    Write-Host "Log: $destination"
    Select-String -LiteralPath $destination -Pattern 'texture_override:|Texture override generation' |
        ForEach-Object { Write-Host $_.Line }
} else {
    Write-Warning 'No new game log was found under the build logs directory.'
}
Write-Host "Game exit code: $($process.ExitCode)"
