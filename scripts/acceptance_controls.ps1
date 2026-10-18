# Controls acceptance: the in-game settings surface the external Controller Settings
# executable used to provide (docs/plans/in-progress/2026-10-17-gameplay-input-mapping-plan.md
# §14, P7).
#
# One boot drives the whole hierarchy the plan asks for and keeps the screenshot it decided on:
#
#   * HELP & OPTIONS -> Controls -> the three pages (Input Types, General Mapping, Gameplay
#     Mapping), each named by the screen the host logs, not by a sleep;
#   * General Mapping -> A Button -> Assign -> the listening dialog -> a keyboard tap binds the
#     row and the profile on disk grows the source;
#   * the same dialog again, held instead of tapped, cancels and writes nothing - that is the
#     dialog's own cancel gesture (capture::kHoldToCancel), and it is why B and Escape stay
#     assignable: a tap of either binds it, a hold of it cancels;
#   * Gameplay Mapping -> Controller -> Custom toggles the custom table on, and Default Presets
#     (the title's own preset page one row above) then refuses to open while Custom is On;
#   * a key pressed into the Controller table is refused by kind and the dialog keeps listening,
#     then leaves on the hold;
#   * Gameplay Mapping -> Keyboard & Mouse -> Custom Mapping -> First Note accepts the same key
#     and persists it in its own table.
#
# Every press is a click on the row the host logs (native_mouse: row=<n> bounds=...), so a step
# names the row it wants instead of counting taps - this title's lists wrap, and a tap count that
# is one off silently presses a different row. Each click re-reads the table, so a screen that
# scrolled still lands on the row it means.
#
# Usage:  pwsh -File scripts\acceptance_controls.ps1 [-Label p7-a]
# Writes out/gameplay-mapping/in-game/<Label>/ (the run's log, argv, and the profile before and
# after each step) and <Label>-shots/*.png, prints a per-step verdict, and exits non-zero unless
# every step behaved.
param(
    [string]$Label = 'p7-controls',
    [string]$BuildDir = 'out/build/win-amd64-release',
    [int]$BootTimeoutSec = 240,
    [int]$StepSeconds = 4
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

# A scratch profile with no input rows at all: every row this run reads is a compiled default,
# and every write the run makes is one it made.
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

# The row table the host logged for the focused list, newest first. A table is a run of
# `row=<n>` lines whose numbers increase; a hover re-logs one row on its own, so only a run that
# contains the wanted index is used.
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

# The screen the host last named. Every step asserts on this rather than on pixels.
function Get-Screen {
    $line = Select-String -Path $log -Pattern 'native_mouse: screen=(\S+)' -ErrorAction SilentlyContinue |
        Select-Object -Last 1
    if (-not $line) { return '' }
    return ([regex]::Match($line.Line, 'screen=(\S+)')).Groups[1].Value
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

function Assert-Profile {
    param([string]$Needle, [bool]$Present, [string]$Tag, [string]$Snapshot)
    $script:steps += 1
    $text = Get-Content -LiteralPath $profilePath -Raw
    $has = $text.ToLowerInvariant().Contains($Needle.ToLowerInvariant())
    if ($has -ne $Present) {
        $script:failures += "${Tag}: the profile should $(if ($Present) { 'contain' } else { 'not contain' }) '$Needle'"
        Write-Host ("  !! {0}: profile {1} '{2}'" -f $Tag, $(if ($Present) { 'lacks' } else { 'still has' }), $Needle)
        return
    }
    Write-Host ("  {0}: profile {1} '{2}'" -f $Tag, $(if ($Present) { 'gained' } else { 'still lacks' }), $Needle)
    if ($Snapshot) { Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outDir $Snapshot) }
}

function Assert-Same {
    param([string]$Path, [string]$Tag)
    $script:steps += 1
    $now = (Get-FileHash -LiteralPath $profilePath -Algorithm SHA256).Hash
    $before = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    if ($now -ne $before) {
        $script:failures += "${Tag}: the profile changed (cancel wrote something)"
        Write-Host "  !! ${Tag}: the profile changed"
        return
    }
    Write-Host "  ${Tag}: the profile is byte-identical"
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
    Start-Sleep -Seconds 4
    Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outDir 'profile-00-boot.toml')

    # The title screen takes A, then the content pipeline's connect/discovery pass runs before the
    # main menu's option list is drawn and its row geometry is logged. A press sent during that
    # pass is dropped (the pipeline stalls), so the harness waits for a row line rather than for a
    # fixed sleep, and tries the press again if the first one was swallowed.
    Drive @('shot:00-title.png')
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        Drive @('key:a')
        $saw_rows = $false
        for ($i = 0; $i -lt 120; $i++) {
            if (Select-String -Path $log -Pattern 'native_mouse: row=0 bounds=' -ErrorAction SilentlyContinue) {
                $saw_rows = $true
                break
            }
            Start-Sleep -Milliseconds 500
        }
        if ($saw_rows) { break }
        Write-Host "  (the menu list has not drawn yet; pressing A again, attempt $attempt)"
    }
    if (-not (Select-String -Path $log -Pattern 'native_mouse: row=0 bounds=' -ErrorAction SilentlyContinue)) {
        throw 'the main menu list never drew a row'
    }
    Start-Sleep -Seconds 1
    Drive @('shot:01-menu.png')

    # --- the hierarchy -----------------------------------------------------------------------
    ClickRow 3 '02-help-and-options'
    Assert-Screen 'settings_screen' '02-help-and-options' | Out-Null
    ClickRow 1 '03-controls'
    Assert-Screen 'settings_controls_screen' '03-controls' | Out-Null

    ClickRow 0 '04-input-types'
    Assert-Screen 'settings_input_types_screen' '04-input-types' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'shot:05-back-controls.png')

    ClickRow 1 '06-general-mapping'
    Assert-Screen 'settings_general_mapping_screen' '06-general-mapping' | Out-Null
    Drive @('key:b', "wait:$StepSeconds")
    ClickRow 2 '07-gameplay-mapping'
    Assert-Screen 'settings_gameplay_mapping_screen' '07-gameplay-mapping' | Out-Null
    Drive @('key:b', "wait:$StepSeconds")

    # --- a binding that binds ----------------------------------------------------------------
    ClickRow 1 '08-general-mapping'
    Assert-Screen 'settings_general_mapping_screen' '08-general-mapping' | Out-Null
    ClickRow 0 '09-a-button'
    Assert-Screen 'settings_bind_general_a_screen' '09-a-button' | Out-Null
    Drive @('shot:10-a-button-rows.png')
    ClickRow 0 '11-assign'
    Assert-Screen 'settings_listen_screen' '11-assign' | Out-Null
    Drive @('shot:12-listening.png', 'key:key_g', "wait:$StepSeconds", 'shot:13-bound.png')
    Assert-Screen 'settings_bind_general_a_screen' '13-bound' | Out-Null
    Assert-Profile 'key:g' $true '13-bound' 'profile-13-after-tap.toml'

    # --- the cancel gesture ------------------------------------------------------------------
    ClickRow 0 '14-assign-again'
    Assert-Screen 'settings_listen_screen' '14-assign-again' | Out-Null
    Drive @('hold:key_g:2', "wait:$StepSeconds", 'shot:15-cancelled.png')
    Assert-Screen 'settings_bind_general_a_screen' '15-cancelled' | Out-Null
    Assert-Same (Join-Path $outDir 'profile-13-after-tap.toml') '15-cancelled'

    # --- the Controller table's own pages ----------------------------------------------------
    Drive @('key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds", 'shot:16-back-controls.png')
    ClickRow 2 '17-gameplay-mapping'
    Assert-Screen 'settings_gameplay_mapping_screen' '17-gameplay-mapping' | Out-Null
    ClickRow 0 '18-controller-page'
    Assert-Screen 'settings_gameplay_controller_screen' '18-controller-page' | Out-Null
    Drive @('shot:19-controller-rows.png')
    # Custom on: the row is a boolean, so one press writes b:1 and commits.
    ClickRow 1 '20-custom-on'
    Assert-Screen 'settings_gameplay_controller_screen' '20-custom-on' | Out-Null
    Assert-Profile 'gameplay_mapping_controller = true' $true '20-custom-on' 'profile-20-custom-on.toml'
    # Default Presets is one row up and must refuse while Custom is On.
    ClickRow 0 '21-default-presets'
    Assert-Screen 'settings_gameplay_controller_screen' '21-default-presets' | Out-Null
    # Custom off: false is the compiled default, so the store drops the row instead of writing a
    # value that says nothing (the profile records only what differs from the defaults).
    ClickRow 1 '22-custom-off'
    Assert-Screen 'settings_gameplay_controller_screen' '22-custom-off' | Out-Null
    Assert-Profile 'gameplay_mapping_controller' $false '22-custom-off' 'profile-22-custom-off.toml'

    # --- a key into the Controller table is the wrong kind ------------------------------------
    ClickRow 2 '23-controller-mapping'
    Assert-Screen 'settings_controller_actions_screen' '23-controller-mapping' | Out-Null
    ClickRow 0 '24-first-note'
    Assert-Screen 'settings_bind_controller_first_note_screen' '24-first-note' | Out-Null
    ClickRow 0 '25-assign-controller'
    Assert-Screen 'settings_listen_screen' '25-assign-controller' | Out-Null
    Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outDir 'profile-25-before-wrong-kind.toml')
    Drive @('shot:26-listening-controller.png', 'key:key_g', "wait:$StepSeconds", 'shot:27-wrong-kind.png')
    Assert-Screen 'settings_listen_screen' '27-wrong-kind' | Out-Null
    Assert-Same (Join-Path $outDir 'profile-25-before-wrong-kind.toml') '27-wrong-kind'
    Drive @('hold:key_g:2', "wait:$StepSeconds", 'shot:28-left-by-hold.png')
    Assert-Screen 'settings_bind_controller_first_note_screen' '28-left-by-hold' | Out-Null

    # --- the Keyboard & Mouse table accepts it ------------------------------------------------
    # Three Backs: the bind page returns to the Controller actions page, that to the Controller
    # page, and that to the Gameplay Mapping page where the sibling Keyboard & Mouse row lives.
    Drive @('key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds", 'shot:29-back-gameplay.png')
    ClickRow 1 '30-keyboard-mouse'
    Assert-Screen 'settings_gameplay_keyboard_mouse_screen' '30-keyboard-mouse' | Out-Null
    Drive @('shot:31-keyboard-mouse-rows.png')
    ClickRow 1 '32-keyboard-mouse-mapping'
    Assert-Screen 'settings_keyboard_mouse_actions_screen' '32-keyboard-mouse-mapping' | Out-Null
    ClickRow 0 '33-first-note-km'
    Assert-Screen 'settings_bind_keyboard_mouse_first_note_screen' '33-first-note-km' | Out-Null
    ClickRow 0 '34-assign-km'
    Assert-Screen 'settings_listen_screen' '34-assign-km' | Out-Null
    Drive @('shot:35-listening-km.png', 'key:key_g', "wait:$StepSeconds", 'shot:36-bound-km.png')
    Assert-Screen 'settings_bind_keyboard_mouse_first_note_screen' '36-bound-km' | Out-Null
    Assert-Profile 'key:g' $true '36-bound-km' 'profile-36-after-km-tap.toml'
    Assert-Profile 'gameplay_remap_keyboard_mouse' $true '36-bound-km' ''

    # Back out to the Controls page and then to the main menu: every level's Back is its own.
    Drive @('key:b', "wait:$StepSeconds", 'shot:37-back-actions.png')
    Drive @('key:b', "wait:$StepSeconds", 'shot:38-back-keyboard-mouse.png')
    Drive @('key:b', "wait:$StepSeconds", 'shot:39-back-gameplay.png')
    Drive @('key:b', "wait:$StepSeconds", 'shot:40-back-controls.png')
    Assert-Screen 'settings_controls_screen' '40-back-controls' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds", 'shot:41-back-menu.png')
    Assert-Screen 'splash_screen' '41-back-menu' | Out-Null
} catch {
    $script:failures += $_.Exception.Message
    Write-Host ("  !! run stopped: {0}" -f $_.Exception.Message)
} finally {
    Start-Sleep -Seconds 2
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500
}

# The bridge's own record of what the guest asked for, kept beside the shots.
if (Test-Path $log) {
    Select-String -Path $log -Pattern 'settings_bridge: (capture|bind-reset|set|commit)' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'bridge-lines.txt')
    Select-String -Path $log -Pattern 'native_mouse: screen=' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'screens.txt')
}
Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outDir 'profile-final.toml')

Write-Host "`n=== Controls acceptance ==="
Write-Host ("steps: {0}, failures: {1}" -f $script:steps, $script:failures.Count)
foreach ($failure in $script:failures) { Write-Host "  - $failure" }
$ok = $script:failures.Count -eq 0
Write-Host ("controls: {0}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }))
Write-Host "evidence: $outDir"
if (-not $ok) { exit 1 }
exit 0
