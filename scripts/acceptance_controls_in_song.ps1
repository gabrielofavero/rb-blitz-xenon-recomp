# In-song Controls acceptance (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md
# §14.1/§16.5, P8a).
#
# P7 built the in-song twins of the whole Controls tree and re-pointed retail's own `pause_controls`
# case at them, but no boot ever opened the page from a paused song (plan §16.5). This arm closes
# that residual:
#
#   pause -> HELP & OPTIONS (retail's `pause_options` row) -> our in-song root -> Controls ->
#   Input Types -> Back -> General Mapping -> A Button -> Assign (the listening dialog) -> hold to
#   cancel -> Back to the root -> Back to the pause panel -> resume to the song.
#
# Every step asserts on the host's `native_mouse: screen=` trace, so a wrong row or a fault is a
# named failure rather than a screenshot to interpret. A guest fault is fatal for the arm: the plan
# says to report it, not paper over it.
#
# Usage:  pwsh -File scripts\acceptance_controls_in_song.ps1 [-Label p8a-in-song] [-Probe]
param(
    [string]$Label = 'p8a-in-song',
    [string]$BuildDir = 'out/build/win-amd64-release',
    [int]$BootTimeoutSec = 240,
    [int]$TitleTimeoutSec = 180,
    [int]$StepSeconds = 3,
    [switch]$Probe
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work 'rb_blitz.exe'
$game = Join-Path $root 'game'
$base = Join-Path $root 'out\gameplay-mapping\in-game'
$outDir = Join-Path $base $Label
$scratch = Join-Path $base "$Label-userdata"
$empty = Join-Path $base 'empty-dlc'
$profilePath = Join-Path $scratch 'launcher.toml'
$log = Join-Path $outDir 'game.log'
$shots = "out/gameplay-mapping/in-game/$Label-shots"

foreach ($p in @($exe, (Join-Path $game 'default.xex'))) {
    if (-not (Test-Path -LiteralPath $p)) { throw "missing $p" }
}
if (Get-Process rb_blitz -ErrorAction SilentlyContinue) { throw 'Close the running game first.' }
if (Test-Path -LiteralPath $outDir) { throw "Use a fresh -Label: $outDir exists (this harness never overwrites evidence)." }
New-Item -ItemType Directory -Force -Path $outDir, $scratch, $empty | Out-Null

Set-Content -Path $profilePath -Value ''
@"
schema_version = 1

[general]
target = 'common'
"@ | Set-Content -Path (Join-Path $scratch 'settings.toml')

$exeArgs = @("--game_data_root=$game", '--mnk_mode=1', '--fullscreen=false',
             "--user_data_root=$scratch", "--launcher_profile=$profilePath",
             "--dlc_library=$empty", "--dlc_root=$empty",
             '--enhancements_dlc_cache=true', '--enhancements_dlc_uncap_enumeration=true',
             "--log_file=$log", '--mouse_native_trace=true')
$exeArgs | Set-Content (Join-Path $outDir 'argv.txt')
New-Item -ItemType Directory -Force -Path (Join-Path $root $shots) | Out-Null

$script:failures = @()
$script:steps = 0

function Drive {
    param([string[]]$Actions)
    & (Join-Path $root 'scripts\drive_ui.ps1') -Actions $Actions -OutDir $shots | Out-Null
}

function Get-Screen {
    $line = Select-String -Path $log -Pattern 'native_mouse: screen=(\S+)' -ErrorAction SilentlyContinue |
        Select-Object -Last 1
    if (-not $line) { return '' }
    return ([regex]::Match($line.Line, 'screen=(\S+)')).Groups[1].Value
}

# The row table the host logged for the focused list, newest first - the same helper the main-menu
# Controls harness uses. Rows are clicked by the bounds the host logs instead of counting taps:
# this title's lists wrap, and a single `lstick_down` tap can travel more than one row (the
# measured reason the first version of this arm landed on "Exit Game").
function Get-RowTable {
    param([int]$Index)
    $lines = Select-String -Path $log -Pattern 'native_mouse: row=(\d+) bounds=' -ErrorAction SilentlyContinue
    if (-not $lines) { return $null }
    $runs = @()
    $current = @()
    $last = -1
    foreach ($line in $lines) {
        $m = [regex]::Match($line.Line, 'row=(\d+) bounds=([0-9.]+),([0-9.]+),([0-9.]+),([0-9.]+)')
        if (-not $m.Success) { continue }
        $n = [int]$m.Groups[1].Value
        if ($n -le $last) { if ($current.Count) { $runs += , $current }; $current = @() }
        $last = $n
        $current += , @($n, [double]$m.Groups[2].Value, [double]$m.Groups[3].Value,
                        [double]$m.Groups[4].Value, [double]$m.Groups[5].Value)
    }
    if ($current.Count) { $runs += , $current }
    for ($i = $runs.Count - 1; $i -ge 0; $i--) {
        $hit = $runs[$i] | Where-Object { $_[0] -eq $Index }
        if ($hit) { return $hit }
    }
    return $null
}

function ClickRow {
    param([int]$Index, [string]$Tag)
    $script:steps += 1
    $hit = $null
    for ($i = 0; $i -lt 10 -and -not $hit; $i++) {
        $hit = Get-RowTable $Index
        if (-not $hit) { Start-Sleep -Seconds 1 }
    }
    if (-not $hit) {
        $script:failures += "${Tag}: the log names no row $Index"
        throw "${Tag}: the log names no row $Index"
    }
    $x = [math]::Round((($hit[1] + $hit[3]) / 2), 4)
    $y = [math]::Round((($hit[2] + $hit[4]) / 2), 4)
    Write-Host ("  {0}: row {1} at {2},{3}" -f $Tag, $Index, $x, $y)
    Drive @("mouse:$x`:$y", 'wait:1', "shot:$Tag-hover.png", 'click:left', "wait:$StepSeconds", "shot:$Tag.png")
}

function Assert-Screen {
    param([string]$Expected, [string]$Tag)
    $script:steps += 1
    $actual = Get-Screen
    if ($actual -ne $Expected) {
        $script:failures += "${Tag}: expected screen '$Expected', the log says '$actual'"
        Write-Host ("  !! {0}: expected {1}, got {2}" -f $Tag, $Expected, $actual)
        return $false
    }
    Write-Host ("  {0}: screen={1}" -f $Tag, $actual)
    return $true
}

function Assert-NoFault {
    param([string]$Tag)
    $script:steps += 1
    $fault = Select-String -Path $log -Pattern 'access violation|Unhandled guest' -ErrorAction SilentlyContinue
    if ($fault) {
        $script:failures += "${Tag}: the guest faulted: $($fault[-1].Line)"
        Write-Host ("  !! {0}: the guest faulted: {1}" -f $Tag, $fault[-1].Line)
    } else {
        Write-Host ("  {0}: no guest fault" -f $Tag)
    }
}

$process = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $exeArgs
$process.Id | Set-Content (Join-Path $outDir 'pid.txt')

try {
    $deadline = (Get-Date).AddSeconds($BootTimeoutSec)
    $booted = $false
    while ((Get-Date) -lt $deadline) {
        if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) { throw 'the game exited during the boot' }
        if ((Test-Path $log) -and (Select-String -Path $log -Pattern 'settings_boot: presented menu frame' -ErrorAction SilentlyContinue)) {
            $booted = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $booted) { throw 'the boot never presented a menu frame' }

    # The guest's title, by OCR: input before it is up is dropped (and is what the known GPU packet
    # desync was first seen with).
    $title = ''
    $deadline = (Get-Date).AddSeconds($TitleTimeoutSec)
    while ((Get-Date) -lt $deadline -and -not ($title -match '(?i)TO START|PRESS')) {
        if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) { throw 'the game exited before the title' }
        Drive @('shot:00-title.png')
        $title = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
            (Join-Path $root 'scripts\ocr_image.ps1') -Path (Join-Path $root "$shots\00-title.png") 2>$null | Out-String
        if (-not ($title -match '(?i)TO START|PRESS')) { Start-Sleep -Seconds 3 }
    }
    if (-not ($title -match '(?i)TO START|PRESS')) { throw 'the guest title was never read' }

    # Into a song, exactly the route the P3/P7 arms use against an empty DLC library.
    $up = @('key:lstick_up', 'wait:1') * 6
    $down = @('key:lstick_down', 'wait:1') * 2
    Drive (@('key:a', 'wait:4', 'key:a', 'wait:10', 'shot:01-songs.png') + $up + $down +
           @('shot:02-selected.png', 'key:a', 'wait:12', 'shot:03-load.png',
             'key:a', 'wait:20', 'shot:04-gameplay.png'))
    Assert-Screen 'game_screen' '04-gameplay' | Out-Null

    # Pause: the game's own pause path, reached through the Mouse & Keyboard table's default.
    # P2 of the interface/gameplay mapping restructure plan moved that default from Return to
    # Escape (D12), and the table is always active in a song, so Escape is what pauses now.
    Drive @('key:esc', "wait:$StepSeconds", 'shot:05-pause.png')
    Assert-Screen 'game_screen' '05-paused' | Out-Null
    $script:steps += 1
    if (-not (Select-String -Path $log -Pattern 'panel=pause_panel' -ErrorAction SilentlyContinue)) {
        $script:failures += '05-paused: the log never named the pause panel'
        Write-Host '  !! 05-paused: the log never named the pause panel'
    } else {
        Write-Host '  05-paused: panel=pause_panel'
    }
    if ($Probe) { throw 'probe stop: the pause menu is on screen' }

    # pause.lst's rows, OCR'd from this boot's own capture: Resume Game (0), Help & Options (1),
    # Achievements (2), Restart (3), Quit Song (4), Main Menu (5), Exit Game (6).
    ClickRow 1 '06-pause-root'
    Assert-Screen 'settings_pause_screen' '06-pause-root' | Out-Null

    # The in-song root's rows, in the merged root's current order (screenshot 06-pause-root.png):
    # How to Play (0), Calibration (1), General (2), Controls (3), Audio/Video (4).
    ClickRow 3 '07-controls'
    Assert-Screen 'settings_controls_pause_screen' '07-controls' | Out-Null

    ClickRow 0 '08-input-types'
    Assert-Screen 'settings_input_types_pause_screen' '08-input-types' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'shot:09-back-controls.png')
    Assert-Screen 'settings_controls_pause_screen' '09-back-controls' | Out-Null

    ClickRow 1 '10-general'
    Assert-Screen 'settings_general_mapping_pause_screen' '10-general' | Out-Null
    ClickRow 0 '11-a-button'
    Assert-Screen 'settings_bind_general_a_pause_screen' '11-a-button' | Out-Null
    ClickRow 0 '12-listening'
    Assert-Screen 'settings_listen_pause_screen' '12-listening' | Out-Null
    # The cancel gesture: a 2 s hold of any key (B included - a tap of it would bind it).
    Drive @('hold:b:2', "wait:$StepSeconds", 'shot:13-cancelled.png')
    Assert-Screen 'settings_bind_general_a_pause_screen' '13-cancelled' | Out-Null

    # Back out: bind page -> General Mapping -> Controls -> in-song root (three levels).
    Drive @('key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds",
           'key:b', "wait:$StepSeconds", 'shot:14-back-root.png')
    Assert-Screen 'settings_pause_screen' '14-back-root' | Out-Null
    # One more Back is the root's own leave: game_screen, with the pause panel still up.
    Drive @('key:b', "wait:$StepSeconds", 'shot:15-back-game.png')
    Assert-Screen 'game_screen' '15-back-game' | Out-Null
    # B on the pause panel's focused pause.lst is retail's own resume.
    Drive @('key:b', "wait:$StepSeconds", 'shot:16-resumed.png')
    Assert-Screen 'game_screen' '16-resumed' | Out-Null
    Start-Sleep -Seconds 2
    $script:steps += 1
    if (-not (Select-String -Path $log -Pattern 'screen=game_screen panel=game_panel' -ErrorAction SilentlyContinue)) {
        $script:failures += '16-resumed: the log never returned to game_panel (the song is still paused)'
        Write-Host '  !! 16-resumed: the log never returned to game_panel (the song is still paused)'
    } else {
        Write-Host '  16-resumed: screen=game_screen panel=game_panel'
    }
    Drive @('shot:17-in-song.png')
    Assert-NoFault '17-in-song'
    $script:steps += 1
    if ($process.HasExited) {
        $script:failures += '17-in-song: the game exited'
        Write-Host '  !! 17-in-song: the game exited'
    } else {
        Write-Host '  17-in-song: the game is still running'
    }
} catch {
    if ($_.Exception.Message -like 'probe stop:*') {
        Write-Host ("  {0}" -f $_.Exception.Message)
    } else {
        $script:failures += $_.Exception.Message
        Write-Host ("  !! run stopped: {0}" -f $_.Exception.Message)
    }
} finally {
    Start-Sleep -Seconds 2
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500
}

if (Test-Path $log) {
    Select-String -Path $log -Pattern 'native_mouse: screen=' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'screens.txt')
    Select-String -Path $log -Pattern 'settings_bridge: (capture|capture-cancel|help)' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'capture-lines.txt')
}

Write-Host "`n=== In-song Controls acceptance ==="
Write-Host ("steps: {0}, failures: {1}" -f $script:steps, $script:failures.Count)
foreach ($failure in $script:failures) { Write-Host "  - $failure" }
$ok = $script:failures.Count -eq 0
Write-Host ("in-song: {0}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }))
Write-Host "evidence: $outDir"
if (-not $ok) { exit 1 }
exit 0
