# Controls acceptance: the in-game settings surface the external Controller Settings
# executable used to provide (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md
# §14, P7).
#
# One boot drives the whole hierarchy the plan asks for and keeps the screenshot it decided on:
#
#   * HELP & OPTIONS -> Controls -> the three pages (Input Types, Interface Mapping, Gameplay
#     Mapping), each named by the screen the host logs, not by a sleep;
#   * Interface Mapping (P3 of the interface/gameplay mapping restructure plan) -> Confirm -> an
#     Assign page -> the listening dialog -> a keyboard tap binds the row and the profile on disk
#     grows the source;
#   * the same dialog again, held instead of tapped, cancels and writes nothing - that is the
#     dialog's own cancel gesture (capture::kHoldToCancel), and it is why B and Escape stay
#     assignable: a tap of either binds it, a hold of it cancels;
#   * Interface Mapping -> Navigation and Music Library Options open their own groups, bind a row
#     each; Reset All then restores every row (all 16 targets, the seven the page hides included)
#     in one press and leaves no `[remap]` row behind;
#   * Gameplay Mapping (P4) -> Controller: Controller Presets (the title's own page, which always
#     opens - there is no Custom switch above it) and Custom Mapping, which lists the three groups
#     (Notes, Move Lane, Pause & Power-up) and a plain Reset All;
#   * a key pressed into the Controller table is refused by kind and the dialog keeps listening,
#     then leaves on the hold;
#   * Gameplay Mapping -> Mouse & Keyboard lists the same groups directly (D6; no Custom Mapping
#     level); Notes -> First Note accepts the same key, persists it in its own table, and the row
#     reads the captured key;
#   * that table's plain Reset All then removes the custom row and the row falls back to its
#     default pair (D9).
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

# P1: the binding-row line the host overlay drew for a page. The overlay logs the whole set once
# per change (`help_overlay: binding rows <screen>: <token> = <text> | ...`), so a step can assert
# on the words the player sees instead of on pixels; the screenshots beside it are the visual
# record. The line is also the state after an in-page capture, which is what makes "the row updates
# without re-entering the page" measurable rather than a claim.
function Get-RowText {
    param([string]$Screen)
    $line = Select-String -Path $log -Pattern "help_overlay: binding rows $Screen" -ErrorAction SilentlyContinue |
        Select-Object -Last 1
    if (-not $line) { return '' }
    return $line.Line
}

function Assert-RowText {
    param([string]$Screen, [string]$Needle, [string]$Tag)
    $script:steps += 1
    $text = Get-RowText $Screen
    if ($text -notlike "*$Needle*") {
        $script:failures += "${Tag}: the row text of ${Screen} lacks '$Needle' (log: '$text')"
        Write-Host ("  !! {0}: row text lacks '{1}'" -f $Tag, $Needle)
        return
    }
    Write-Host ("  {0}: row text has '{1}'" -f $Tag, $Needle)
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
    # The merged root's order is tutorial, calibration, General, Controls, Audio/Video, Gameplay.
    ClickRow 3 '03-controls'
    Assert-Screen 'settings_controls_screen' '03-controls' | Out-Null

    ClickRow 0 '04-input-types'
    Assert-Screen 'settings_input_types_screen' '04-input-types' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'shot:05-back-controls.png')

    ClickRow 1 '06-interface-mapping'
    Assert-Screen 'settings_interface_mapping_screen' '06-interface-mapping' | Out-Null
    # P3: the page names the interface's actions, not the pad's plastic (`Confirm` is the A
    # target's row), and P1's overlay draws the row's own list over the label.
    Assert-RowText 'settings_interface_mapping_screen' 'input.general.a = Confirm: A, Enter, A' '06-interface-mapping'
    Drive @('key:b', "wait:$StepSeconds")
    ClickRow 2 '07-gameplay-mapping'
    Assert-Screen 'settings_gameplay_mapping_screen' '07-gameplay-mapping' | Out-Null
    Drive @('key:b', "wait:$StepSeconds")

    # --- a binding that binds ----------------------------------------------------------------
    ClickRow 1 '08-interface-mapping'
    Assert-Screen 'settings_interface_mapping_screen' '08-interface-mapping' | Out-Null
    # P3: Navigation (0), Confirm (1), Go Back (2), Music Library Options (3), Reset All (4).
    ClickRow 1 '09-confirm-row'
    Assert-Screen 'settings_bind_general_a_screen' '09-confirm-row' | Out-Null
    Drive @('shot:10-confirm-rows.png')
    ClickRow 0 '11-assign'
    Assert-Screen 'settings_listen_screen' '11-assign' | Out-Null
    Drive @('shot:12-listening.png', 'key:key_g', "wait:$StepSeconds", 'shot:13-bound.png')
    Assert-Screen 'settings_bind_general_a_screen' '13-bound' | Out-Null
    Assert-Profile 'key:g' $true '13-bound' 'profile-13-after-tap.toml'

    # P1: the page behind the bind page draws the captured source in the row without a relaunch -
    # the row text is read from the published snapshot every frame, not baked when the page opened.
    # P2/D8: the FIRST Assign on a default row REPLACES the default list, so the row reads
    # `Confirm: G` rather than the default's three sources plus the captured one.
    Drive @('key:b', "wait:$StepSeconds", 'shot:13a-interface-row-after-capture.png')
    Assert-Screen 'settings_interface_mapping_screen' '13a-interface-row-after-capture' | Out-Null
    Assert-RowText 'settings_interface_mapping_screen' 'input.general.a = Confirm: G' '13a-interface-row-after-capture'
    ClickRow 1 '13b-confirm-row-again'
    Assert-Screen 'settings_bind_general_a_screen' '13b-confirm-row-again' | Out-Null

    # --- the cancel gesture ------------------------------------------------------------------
    ClickRow 0 '14-assign-again'
    Assert-Screen 'settings_listen_screen' '14-assign-again' | Out-Null
    Drive @('hold:key_g:2', "wait:$StepSeconds", 'shot:15-cancelled.png')
    Assert-Screen 'settings_bind_general_a_screen' '15-cancelled' | Out-Null
    Assert-Same (Join-Path $outDir 'profile-13-after-tap.toml') '15-cancelled'

    # --- the two Interface Mapping groups and Reset All (P3) ----------------------------------
    Drive @('key:b', "wait:$StepSeconds", 'shot:15a-back-interface.png')
    Assert-Screen 'settings_interface_mapping_screen' '15a-back-interface' | Out-Null
    ClickRow 0 '15b-navigation'
    Assert-Screen 'settings_interface_navigation_screen' '15b-navigation' | Out-Null
    Assert-RowText 'settings_interface_navigation_screen' 'input.general.dpad_up = Up: D-pad Up, W, Arrow Up' '15b-navigation'
    ClickRow 0 '15c-up-row'
    Assert-Screen 'settings_bind_general_dpad_up_screen' '15c-up-row' | Out-Null
    ClickRow 0 '15d-assign-up'
    Assert-Screen 'settings_listen_screen' '15d-assign-up' | Out-Null
    Drive @('key:key_j', "wait:$StepSeconds", 'shot:15e-up-bound.png')
    Assert-Screen 'settings_bind_general_dpad_up_screen' '15e-up-bound' | Out-Null
    Assert-Profile 'key:j' $true '15e-up-bound' 'profile-15e-after-up-tap.toml'
    # One Back is the bind page's own ``leave``: it returns to whoever sent it, which is Navigation
    # (the page that lists the Up row), not the Interface Mapping page above it.
    Drive @('key:b', "wait:$StepSeconds", 'shot:15f-navigation-rows.png')
    Assert-Screen 'settings_interface_navigation_screen' '15f-navigation-rows' | Out-Null
    Assert-RowText 'settings_interface_navigation_screen' 'input.general.dpad_up = Up: J' '15f-navigation-rows'
    Drive @('key:b', "wait:$StepSeconds", 'shot:15g-back-interface.png')
    Assert-Screen 'settings_interface_mapping_screen' '15g-back-interface' | Out-Null
    ClickRow 3 '15h-music-library'
    Assert-Screen 'settings_interface_music_library_screen' '15h-music-library' | Out-Null
    Assert-RowText 'settings_interface_music_library_screen' 'input.general.start = Search: Start, Space' '15h-music-library'
    Drive @('key:b', "wait:$StepSeconds", 'shot:15i-back-interface.png')
    Assert-Screen 'settings_interface_mapping_screen' '15i-back-interface' | Out-Null
    # D9 + D2: one plain `Reset All` press restores every row - the two captures above and the
    # seven targets this page does not draw - and removes the `[remap]` rows from the profile.
    ClickRow 4 '15j-reset-all'
    Assert-Screen 'settings_interface_mapping_screen' '15j-reset-all' | Out-Null
    Assert-RowText 'settings_interface_mapping_screen' 'input.general.a = Confirm: A, Enter, A' '15j-reset-all'
    Assert-Profile 'key:j' $false '15j-reset-all' 'profile-15j-after-reset-all.toml'

    # --- the Controller table's own pages ----------------------------------------------------
    # Reset All left us on the Interface Mapping page, so one Back is the Controls page.
    Drive @('key:b', "wait:$StepSeconds", 'shot:16-back-controls.png')
    ClickRow 2 '17-gameplay-mapping'
    Assert-Screen 'settings_gameplay_mapping_screen' '17-gameplay-mapping' | Out-Null
    # P4: two input types; Controller = Controller Presets (0) + Custom Mapping (1).
    ClickRow 0 '18-controller-page'
    Assert-Screen 'settings_gameplay_controller_screen' '18-controller-page' | Out-Null
    Drive @('shot:19-controller-rows.png')
    # P4/D5: the preset page is always available - a plain retail route, no Custom switch above it.
    ClickRow 0 '20-controller-presets'
    Assert-Screen 'controller_config_screen' '20-controller-presets' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'shot:21-back-from-presets.png')
    Assert-Screen 'settings_gameplay_controller_screen' '21-back-from-presets' | Out-Null

    # P4/D7/D9: Custom Mapping is Notes (0), Move Lane (1), Pause & Power-up (2), Reset All (3).
    ClickRow 1 '23-controller-mapping'
    Assert-Screen 'settings_controller_actions_screen' '23-controller-mapping' | Out-Null
    Drive @('shot:24-controller-groups.png')
    ClickRow 0 '25-notes'
    Assert-Screen 'settings_controller_notes_screen' '25-notes' | Out-Null
    # P1: a list too long for the row reads `Multiple` (the owner's allowance) instead of
    # overflowing; the Controller default for First Note is eight stick/D-pad directions.
    Assert-RowText 'settings_controller_notes_screen' 'input.controller.first_note = Multiple' '25-notes'
    # --- a key into the Controller table is the wrong kind ------------------------------------
    ClickRow 0 '26-first-note'
    Assert-Screen 'settings_bind_controller_first_note_screen' '26-first-note' | Out-Null
    ClickRow 0 '27-assign-controller'
    Assert-Screen 'settings_listen_screen' '27-assign-controller' | Out-Null
    Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outDir 'profile-27-before-wrong-kind.toml')
    Drive @('shot:28-listening-controller.png', 'key:key_g', "wait:$StepSeconds", 'shot:29-wrong-kind.png')
    Assert-Screen 'settings_listen_screen' '29-wrong-kind' | Out-Null
    Assert-Same (Join-Path $outDir 'profile-27-before-wrong-kind.toml') '29-wrong-kind'
    Drive @('hold:key_g:2', "wait:$StepSeconds", 'shot:30-left-by-hold.png')
    Assert-Screen 'settings_bind_controller_first_note_screen' '30-left-by-hold' | Out-Null

    # --- the Mouse & Keyboard table accepts it -----------------------------------------------
    # Four Backs: the bind page returns to the group that lists it (Notes), that to the Controller
    # Custom Mapping page, that to the Controller page, and that to Gameplay Mapping.
    Drive @('key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds", 'shot:31-back-gameplay.png')
    Assert-Screen 'settings_gameplay_mapping_screen' '31-back-gameplay' | Out-Null
    ClickRow 1 '32-keyboard-mouse'
    Assert-Screen 'settings_gameplay_keyboard_mouse_screen' '32-keyboard-mouse' | Out-Null
    # P4/D6: the same three groups and Reset All, entered directly - there is no Custom Mapping
    # level here and no Custom switch to turn on first.
    Drive @('shot:33-keyboard-mouse-groups.png')
    ClickRow 0 '34-notes-km'
    Assert-Screen 'settings_keyboard_mouse_notes_screen' '34-notes-km' | Out-Null
    Assert-RowText 'settings_keyboard_mouse_notes_screen' 'input.keyboard_mouse.first_note = First Note: Q, Right Click' '34-notes-km'
    ClickRow 0 '35-first-note-km'
    Assert-Screen 'settings_bind_keyboard_mouse_first_note_screen' '35-first-note-km' | Out-Null
    ClickRow 0 '36-assign-km'
    Assert-Screen 'settings_listen_screen' '36-assign-km' | Out-Null
    Drive @('shot:37-listening-km.png', 'key:key_g', "wait:$StepSeconds", 'shot:38-bound-km.png')
    Assert-Screen 'settings_bind_keyboard_mouse_first_note_screen' '38-bound-km' | Out-Null
    Assert-Profile 'key:g' $true '38-bound-km' 'profile-38-after-km-tap.toml'
    Assert-Profile 'gameplay_remap_keyboard_mouse' $true '38-bound-km' ''
    # D8: the first Assign replaced the default pair, so the group's row now reads the captured key.
    Drive @('key:b', "wait:$StepSeconds", 'shot:39-notes-after-capture.png')
    Assert-Screen 'settings_keyboard_mouse_notes_screen' '39-notes-after-capture' | Out-Null
    Assert-RowText 'settings_keyboard_mouse_notes_screen' 'input.keyboard_mouse.first_note = First Note: G' '39-notes-after-capture'

    # P4/D9: the table's plain Reset All removes the custom row and the row falls back to its
    # default pair - the check the old disabled-when-uncustomised Reset All row could not make.
    Drive @('key:b', "wait:$StepSeconds", 'shot:40-back-groups.png')
    Assert-Screen 'settings_gameplay_keyboard_mouse_screen' '40-back-groups' | Out-Null
    ClickRow 3 '41-reset-all-km'
    Assert-Screen 'settings_gameplay_keyboard_mouse_screen' '41-reset-all-km' | Out-Null
    # The profile keeps a section header it already knew (an empty `[remap]` survived P3's Reset
    # All the same way), so the proof is that the captured row's value is gone - and the row text
    # below shows the default pair again.
    Assert-Profile 'key:g' $false '41-reset-all-km' 'profile-41-after-reset-all.toml'
    ClickRow 0 '42-notes-again'
    Assert-Screen 'settings_keyboard_mouse_notes_screen' '42-notes-again' | Out-Null
    Assert-RowText 'settings_keyboard_mouse_notes_screen' 'input.keyboard_mouse.first_note = First Note: Q, Right Click' '42-notes-again'

    # Back out to the Controls page and then to the main menu: every level's Back is its own.
    Drive @('key:b', "wait:$StepSeconds", 'shot:43-back-groups.png')
    Assert-Screen 'settings_gameplay_keyboard_mouse_screen' '43-back-groups' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'shot:44-back-gameplay.png')
    Assert-Screen 'settings_gameplay_mapping_screen' '44-back-gameplay' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'shot:45-back-controls.png')
    Assert-Screen 'settings_controls_screen' '45-back-controls' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'key:b', "wait:$StepSeconds", 'shot:46-back-menu.png')
    Assert-Screen 'splash_screen' '46-back-menu' | Out-Null
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
    Select-String -Path $log -Pattern 'settings_bridge: (capture|bind[-_]reset|set|commit)' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'bridge-lines.txt')
    Select-String -Path $log -Pattern 'native_mouse: screen=' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'screens.txt')
    # P1: what each binding page drew, in order, as the evidence behind the row screenshots.
    Select-String -Path $log -Pattern 'help_overlay: binding rows ' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'binding-rows.txt')
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
