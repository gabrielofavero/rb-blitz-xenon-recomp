# Exercise the current Controls hierarchy with disposable input bindings.
# Verify layout, presets, touch, menu/gameplay capture, explicit replacement,
# timeout without writes, wheel append, reset, live row text and Back routes.
# Physical controller capture and its first-use warning require a controller;
# this keyboard/mouse route does not establish that device behavior.
param(
    [string]$Label = 'p7-controls',
    [string]$BuildDir = 'out/build/win-amd64-release',
    [int]$BootTimeoutSec = 240,
    [int]$StepSeconds = 4,
    [ValidateSet("", "d3d12", "vulkan")][string]$Backend = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $root 'scripts/diagnostics/file_hash.ps1')
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
             '--enhancements_dlc_uncap_enumeration=true',
             "--log_file=$log", '--mouse_native_trace=true', '--log_flush_interval=1')
if ($Backend) { $exeArgs += "--gpu_backend=$Backend" }
$exeArgs | Set-Content (Join-Path $outDir 'argv.txt')
New-Item -ItemType Directory -Force -Path (Join-Path $root $shots) | Out-Null

$script:failures = @()
$script:steps = 0

function Drive {
    param([string[]]$Actions)
    # These fixtures use the compiled keyboard defaults, independent of legacy MnK pad keys.
    $Actions = @($Actions | ForEach-Object {
        if ($_ -eq 'key:a') { 'key:key_enter' }
        elseif ($_ -eq 'key:b') { 'key:esc' }
        else { $_ }
    })
    & (Join-Path $root 'scripts\diagnostics\drive_ui.ps1') -Actions $Actions -OutDir $shots | Out-Null
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
    param([int]$Index, [string]$Tag, [double]$Wait = -1)
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
    # A Capture row opens a dialog that closes itself after 3 s (P5), so a step that must act
    # inside it settles for less than $StepSeconds.
    $settle = if ($Wait -ge 0) { $Wait } else { $StepSeconds }
    Write-Host ("  {0}: row {1} at {2},{3}" -f $Tag, $Index, $x, $y)
    $script:stepScreenEvents = @(Select-String -LiteralPath $log -Pattern 'native_mouse: screen=').Count
    Drive @("mouse:$x`:$y", 'wait:1', "shot:$Tag-hover.png", 'click:left', "wait:$settle", "shot:$Tag.png")
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
    # Log writes are asynchronous. Validate short-lived entry after the capture
    # action so waiting for disk cannot consume its three-second input window.
    if ($script:pendingListening) {
        $pending = $script:pendingListening
        $pattern = 'native_mouse: screen=' + [regex]::Escape($pending.Screen) + ' .*available=true'
        $seen = $false
        for ($poll = 0; $poll -lt 15; $poll++) {
            $recent = @(Select-String -LiteralPath $log -Pattern 'native_mouse: screen=' |
                Select-Object -Skip $pending.Boundary | ForEach-Object { $_.Line })
            if (@($recent -match $pattern).Count -gt 0) { $seen = $true; break }
            Start-Sleep -Milliseconds 100
        }
        if (-not $seen) { $script:failures += "$($pending.Tag): listening dialog entry was not recorded during this step" }
        else { Write-Host "  $($pending.Tag): bounded listening dialog entry confirmed" }
        $script:pendingListening = $null
    }
    $actual = Get-Screen
    if ($Expected -notmatch '^settings_listen') {
        for ($poll = 0; $poll -lt 15 -and $actual -ne $Expected; $poll++) {
            Start-Sleep -Milliseconds 100
            $actual = Get-Screen
        }
    }
    if ($actual -ne $Expected -and $Expected -match '^settings_listen') {
        $script:pendingListening = @{Screen=$Expected; Tag=$Tag; Boundary=$script:stepScreenEvents}
        Write-Host "  ${Tag}: listening entry queued for validation after input/countdown"
        return $true
    }
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
    $now = (Get-Sha256 $profilePath)
    $before = (Get-Sha256 $Path)
    if ($now -ne $before) {
        $script:failures += "${Tag}: the profile changed (cancel wrote something)"
        Write-Host "  !! ${Tag}: the profile changed"
        return
    }
    Write-Host "  ${Tag}: the profile is byte-identical"
}

# The bridge logs readable source names before converting binding rows to font glyphs.
# Screenshots remain the evidence for guest rendering and layout.
function Get-RowText {
    param([string]$Screen)
    $line = Select-String -Path $log -Pattern "settings_bridge: binding sources $($Screen -replace '_screen$', '_panel'):" -ErrorAction SilentlyContinue |
        Select-Object -Last 1
    if (-not $line) {
        $line = Select-String -Path $log -Pattern "settings_bridge: binding text $($Screen -replace '_screen$', '_panel'):" -ErrorAction SilentlyContinue |
            Select-Object -Last 1
    }
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
    # The six-entry root places Controls after Game and Calibration.
    ClickRow 2 '03-controls'
    Assert-Screen 'settings_controls_screen' '03-controls' | Out-Null

    ClickRow 0 '04-layout'
    Assert-Screen 'settings_controller_layout_screen' '04-layout' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'shot:05-back-controls.png')

    ClickRow 2 '06-interface-mapping'
    Assert-Screen 'settings_interface_mapping_screen' '06-interface-mapping' | Out-Null
    # The page names the interface's actions, not the pad's plastic (`Confirm` is the A
    # target's row), and the guest draws the row's own list.
    Assert-RowText 'settings_interface_mapping_screen' 'Confirm: A, Enter, A' '06-interface-mapping'
    Drive @('key:b', "wait:$StepSeconds")
    ClickRow 3 '07-gameplay-mapping'
    Assert-Screen 'settings_gameplay_mapping_screen' '07-gameplay-mapping' | Out-Null
    Drive @('key:b', "wait:$StepSeconds")

    # --- a binding that binds ----------------------------------------------------------------
    ClickRow 2 '08-interface-mapping'
    Assert-Screen 'settings_interface_mapping_screen' '08-interface-mapping' | Out-Null
    # Navigation (0), Confirm (1), Back (2), Library (3), Music (4), Reset All (5).
    ClickRow 1 '09-confirm-row'
    Assert-Screen 'settings_bind_general_a_screen' '09-confirm-row' | Out-Null
    Drive @('shot:10-confirm-rows.png')
    # P5: the dialog closes itself after 3 s, so every Assign click settles for less than that and
    # the press that binds is sent inside the window.
    ClickRow 1 '11-assign' -Wait 0.4
    Assert-Screen 'settings_listen_screen' '11-assign' | Out-Null
    Drive @('shot:12-listening.png', 'key:key_g', "wait:2", 'shot:13-bound.png')
    Assert-Screen 'settings_bind_general_a_screen' '13-bound' | Out-Null
    Assert-Profile 'key:g' $true '13-bound' 'profile-13-after-tap.toml'

    # P1: the page behind the bind page draws the captured source in the row without a relaunch -
    # the row text is read from the published snapshot every frame, not baked when the page opened.
    # Explicit Replace discards the previous input list, so the row reads
    # `Confirm: G` rather than the default's three sources plus the captured one.
    Drive @('key:b', "wait:$StepSeconds", 'shot:13a-interface-row-after-capture.png')
    Assert-Screen 'settings_interface_mapping_screen' '13a-interface-row-after-capture' | Out-Null
    Assert-RowText 'settings_interface_mapping_screen' 'Confirm: G' '13a-interface-row-after-capture'
    ClickRow 1 '13b-confirm-row-again'
    Assert-Screen 'settings_bind_general_a_screen' '13b-confirm-row-again' | Out-Null

    # --- the countdown (P5) ------------------------------------------------------------------
    ClickRow 0 '14-assign-again' -Wait 0.4
    Assert-Screen 'settings_listen_screen' '14-assign-again' | Out-Null
    # Three seconds of silence close the dialog and write nothing (D11); the profile snapshot from
    # the previous capture is the control. (Holding G here would now BIND it.)
    Drive @('shot:14a-countdown.png', "wait:$StepSeconds", 'shot:15-countdown-left.png')
    Assert-Screen 'settings_bind_general_a_screen' '15-countdown-left' | Out-Null
    Assert-Same (Join-Path $outDir 'profile-13-after-tap.toml') '15-countdown-left'

    # --- the two Interface Mapping groups and Reset All (P3) ----------------------------------
    Drive @('key:b', "wait:$StepSeconds", 'shot:15a-back-interface.png')
    Assert-Screen 'settings_interface_mapping_screen' '15a-back-interface' | Out-Null
    ClickRow 0 '15b-navigation'
    Assert-Screen 'settings_interface_navigation_screen' '15b-navigation' | Out-Null
    Assert-RowText 'settings_interface_navigation_screen' 'Up: D-pad Up, W, Arrow Up' '15b-navigation'
    ClickRow 0 '15c-up-row'
    Assert-Screen 'settings_bind_general_dpad_up_screen' '15c-up-row' | Out-Null
    ClickRow 1 '15d-assign-up' -Wait 0.4
    Assert-Screen 'settings_listen_screen' '15d-assign-up' | Out-Null
    Drive @('key:key_j', "wait:2", 'shot:15e-up-bound.png')
    Assert-Screen 'settings_bind_general_dpad_up_screen' '15e-up-bound' | Out-Null
    Assert-Profile 'key:j' $true '15e-up-bound' 'profile-15e-after-up-tap.toml'
    # One Back is the bind page's own ``leave``: it returns to whoever sent it, which is Navigation
    # (the page that lists the Up row), not the Interface Mapping page above it.
    Drive @('key:b', "wait:$StepSeconds", 'shot:15f-navigation-rows.png')
    Assert-Screen 'settings_interface_navigation_screen' '15f-navigation-rows' | Out-Null
    Assert-RowText 'settings_interface_navigation_screen' 'Up: J' '15f-navigation-rows'
    Drive @('key:b', "wait:$StepSeconds", 'shot:15g-back-interface.png')
    Assert-Screen 'settings_interface_mapping_screen' '15g-back-interface' | Out-Null
    ClickRow 3 '15h-music-library'
    Assert-Screen 'settings_interface_music_library_screen' '15h-music-library' | Out-Null
    Assert-RowText 'settings_interface_music_library_screen' 'Search: Start, Space' '15h-music-library'
    Drive @('key:b', "wait:$StepSeconds", 'shot:15i-back-interface.png')
    Assert-Screen 'settings_interface_mapping_screen' '15i-back-interface' | Out-Null
    # D9 + D2: one plain `Reset All` press restores every row - the two captures above and the
    # seven targets this page does not draw - and removes the `[remap]` rows from the profile.
    ClickRow 5 '15j-reset-all'
    Assert-Screen 'settings_interface_mapping_screen' '15j-reset-all' | Out-Null
    Assert-RowText 'settings_interface_mapping_screen' 'Confirm: A, Enter, A' '15j-reset-all'
    Assert-Profile 'key:j' $false '15j-reset-all' 'profile-15j-after-reset-all.toml'

    # Controller presets and touch remain direct Controls branches. Gameplay bindings
    # use one action list; controller captures select their table from the device.
    Drive @('key:b', "wait:$StepSeconds", 'shot:16-back-controls.png')
    Assert-Screen 'settings_controls_screen' '16-back-controls' | Out-Null
    ClickRow 1 '20-controller-presets'
    Assert-Screen 'controller_config_screen' '20-controller-presets' | Out-Null
    Drive @('key:b', "wait:$StepSeconds", 'shot:21-back-from-presets.png')
    Assert-Screen 'settings_controls_screen' '21-back-from-presets' | Out-Null
    # This Windows route exposes four Controls rows; touch is filtered by platform.
    # Native touch navigation remains an explicit unavailable-platform check.
    Write-Host '  Touch: not exposed by this Windows target'
    ClickRow 3 '32-gameplay-bindings'
    Assert-Screen 'settings_gameplay_mapping_screen' '32-gameplay-bindings' | Out-Null
    # P4/D6: the same three groups and Reset All, entered directly - there is no Custom Mapping
    # level here and no Custom switch to turn on first.
    Drive @('shot:33-keyboard-mouse-groups.png')
    ClickRow 0 '34-notes-km'
    Assert-Screen 'settings_keyboard_mouse_notes_screen' '34-notes-km' | Out-Null
    Assert-RowText 'settings_keyboard_mouse_notes_screen' 'First Note: Q, Right Click' '34-notes-km'
    ClickRow 0 '35-first-note-km'
    Assert-Screen 'settings_bind_keyboard_mouse_first_note_screen' '35-first-note-km' | Out-Null
    ClickRow 1 '36-assign-km' -Wait 0.4
    Assert-Screen 'settings_listen_screen' '36-assign-km' | Out-Null
    # P5/D11: the press itself is the answer - no release wait - and the dialog leaves at once.
    Drive @('shot:37-listening-km.png', 'key:key_g', "wait:2", 'shot:38-bound-km.png')
    Assert-Screen 'settings_bind_keyboard_mouse_first_note_screen' '38-bound-km' | Out-Null
    Assert-Profile 'key:g' $true '38-bound-km' 'profile-38-after-km-tap.toml'
    Assert-Profile 'gameplay_remap_keyboard_mouse' $true '38-bound-km' ''
    # Explicit Replace leaves the captured key as the group row's input.
    Drive @('key:b', "wait:$StepSeconds", 'shot:39-notes-after-capture.png')
    Assert-Screen 'settings_keyboard_mouse_notes_screen' '39-notes-after-capture' | Out-Null
    Assert-RowText 'settings_keyboard_mouse_notes_screen' 'First Note: G' '39-notes-after-capture'

    # P5/D11: a wheel detent is a press with no release, and it binds on the frame it arrives.
    # The row is already custom, so this is also D8's append case (`G, Scroll Up`).
    ClickRow 0 '39a-first-note-again' -Wait 0.4
    Assert-Screen 'settings_bind_keyboard_mouse_first_note_screen' '39a-first-note-again' | Out-Null
    ClickRow 0 '39b-assign-wheel' -Wait 0.4
    Assert-Screen 'settings_listen_screen' '39b-assign-wheel' | Out-Null
    Drive @('shot:39c-listening-wheel.png', 'wheel:1', "wait:2", 'shot:39d-wheel-bound.png')
    Assert-Screen 'settings_bind_keyboard_mouse_first_note_screen' '39d-wheel-bound' | Out-Null
    Assert-Profile 'wheel_up' $true '39d-wheel-bound' 'profile-39d-after-wheel.toml'
    Drive @('key:b', "wait:$StepSeconds", 'shot:39e-notes-wheel.png')
    Assert-Screen 'settings_keyboard_mouse_notes_screen' '39e-notes-wheel' | Out-Null
    Assert-RowText 'settings_keyboard_mouse_notes_screen' 'First Note: G, Scroll Up' '39e-notes-wheel'

    # P4/D9: the table's plain Reset All removes the custom row and the row falls back to its
    # default pair - the check the old disabled-when-uncustomised Reset All row could not make.
    Drive @('key:b', "wait:$StepSeconds", 'shot:40-back-groups.png')
    Assert-Screen 'settings_gameplay_mapping_screen' '40-back-groups' | Out-Null
    ClickRow 3 '41-reset-all-km'
    Assert-Screen 'settings_gameplay_mapping_screen' '41-reset-all-km' | Out-Null
    # The profile keeps a section header it already knew (an empty `[remap]` survived P3's Reset
    # All the same way), so the proof is that the captured row's value is gone - and the row text
    # below shows the default pair again.
    Assert-Profile 'key:g' $false '41-reset-all-km' 'profile-41-after-reset-all.toml'
    Assert-Profile 'wheel_up' $false '41-reset-all-km' ''
    ClickRow 0 '42-notes-again'
    Assert-Screen 'settings_keyboard_mouse_notes_screen' '42-notes-again' | Out-Null
    Assert-RowText 'settings_keyboard_mouse_notes_screen' 'First Note: Q, Right Click' '42-notes-again'

    # Back out to the Controls page and then to the main menu: every level's Back is its own.
    Drive @('key:b', "wait:$StepSeconds", 'shot:43-back-groups.png')
    Assert-Screen 'settings_gameplay_mapping_screen' '43-back-groups' | Out-Null
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
    # Text supplied to each guest binding page, alongside its screenshot.
    Select-String -Path $log -Pattern 'settings_bridge: binding text ' -ErrorAction SilentlyContinue |
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
