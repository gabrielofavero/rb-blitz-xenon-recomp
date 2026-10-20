# P1 acceptance: the runtime binding-row text (docs/plans/in-progress/2026-10-19-interface-gameplay-
# mapping-restructure-plan.md, P1).
#
# One boot proves the three facts P1 has to prove, and keeps the screenshot and the host's own
# line for each:
#
#   * a binding row draws the inputs it holds, not a state word - a seeded two-source [remap] row
#     reads `A: G, H` and a compiled default reads `Left bumper: LB`;
#   * a capture writes the row's text: the A row gains `J` after a tap in the listening dialog,
#     with no relaunch and no re-entry from the root (the page behind the dialog shows it);
#   * a list too long for the row reads `Multiple` - the Controller table's First Note default is
#     eight stick/D-pad directions.
#
# The row text is asserted on the host's own line (`help_overlay: binding rows <screen>: ...`),
# so a failure names the text rather than a pixel; the screenshots are the visual record beside it.
#
# Usage:  pwsh -File scripts\acceptance_binding_rows.ps1 [-Label p1-rows]
# Writes out/interface-mapping/p1-rows/<Label>/ (log, argv, profile snapshots, binding-rows.txt)
# and out/interface-mapping/p1-rows/<Label>-shots/*.png, prints a per-step verdict, and exits
# non-zero unless every step behaved.
param(
    [string]$Label = 'p1-rows',
    [string]$BuildDir = 'out/build/win-amd64-release',
    [int]$BootTimeoutSec = 240,
    [int]$StepSeconds = 4
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work 'rb_blitz.exe'
$game = Join-Path $root 'game'
$base = Join-Path $root 'out\interface-mapping\p1-rows'
$outDir = Join-Path $base $Label
$scratch = Join-Path $base "$Label-userdata"
$empty = Join-Path $base 'empty-dlc'
$profilePath = Join-Path $scratch 'launcher.toml'
$log = Join-Path $outDir 'game.log'
$shots = "out/interface-mapping/p1-rows/$Label-shots"

foreach ($p in @($exe, (Join-Path $game 'default.xex'))) {
    if (-not (Test-Path -LiteralPath $p)) { throw "missing $p" }
}
if (Get-Process rb_blitz -ErrorAction SilentlyContinue) { throw 'Close the running game first.' }
if (Test-Path -LiteralPath $outDir) { throw "Use a fresh -Label: $outDir exists (this harness never overwrites evidence)." }
New-Item -ItemType Directory -Force -Path $outDir, $scratch, $empty | Out-Null

# The fixture is the point: `a` is a stored two-source row (so the overlay must say G, H, not the
# compiled defaults around it), and every other row is untouched.
@"
schema_version = 1

[general]
target = 'common'

[remap]
a = 'key:g, key:h'
"@ | Set-Content -Path $profilePath
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

# The row table the host logged for the focused list, newest run first (same rule as
# acceptance_controls.ps1: a hover re-logs one row, so only a run containing the index counts).
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
    # A Capture row opens a dialog that closes itself after 3 s (P5), so the Assign click settles
    # for less than that.
    $settle = if ($Wait -ge 0) { $Wait } else { $StepSeconds }
    Write-Host ("  {0}: row {1} at {2},{3}" -f $Tag, $Index, $x, $y)
    Drive @("mouse:$x`:$y", 'wait:1', "shot:$Tag-hover.png", 'click:left', "wait:$settle", "shot:$Tag.png")
}

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

function Assert-RowText {
    param([string]$Screen, [string]$Needle, [string]$Tag)
    $script:steps += 1
    $line = Select-String -Path $log -Pattern "help_overlay: binding rows $Screen" -ErrorAction SilentlyContinue |
        Select-Object -Last 1
    $text = if ($line) { $line.Line } else { '' }
    if ($text -notlike "*$Needle*") {
        $script:failures += "${Tag}: the row text of ${Screen} lacks '$Needle' (log: '$text')"
        Write-Host ("  !! {0}: row text lacks '{1}'" -f $Tag, $Needle)
        return
    }
    Write-Host ("  {0}: row text has '{1}'" -f $Tag, $Needle)
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

    # The title takes A; the content pass delays the main menu's first draw, so wait for a row
    # line and press again if the first A was swallowed (the same rule acceptance_controls uses).
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

    # --- HELP & OPTIONS -> Controls -> Interface Mapping --------------------------------------
    # The merged root's order is tutorial, calibration, General, Controls, Audio/Video, Gameplay:
    # Controls is row 3. Asserting the destination after every press is what makes a reordered
    # root a named failure rather than a wrong page that still "works".
    ClickRow 3 '02-help-and-options'
    Assert-Screen 'settings_screen' '02-help-and-options' | Out-Null
    ClickRow 3 '03-controls'
    Assert-Screen 'settings_controls_screen' '03-controls' | Out-Null
    ClickRow 1 '04-interface-mapping'
    Assert-Screen 'settings_interface_mapping_screen' '04-interface-mapping' | Out-Null
    Drive @("wait:$StepSeconds", 'shot:05-interface-rows.png')
    # The seeded two-source row reads its own list under P3's own name for the target (`Confirm`
    # is the A row), and a default row names every source. The wrapped list draws only the rows
    # around the selection, so the default probe is one of those.
    Assert-RowText 'settings_interface_mapping_screen' 'input.general.a = Confirm: G, H' '05-interface-rows'
    Assert-RowText 'settings_interface_mapping_screen' 'input.general.b = Go Back: B, Escape, B' '05-interface-rows'
    Copy-Item -LiteralPath $log -Destination (Join-Path $outDir 'game-before-capture.log')

    # --- a capture changes the row's text ----------------------------------------------------
    ClickRow 1 '06-confirm-row'
    Assert-Screen 'settings_bind_general_a_screen' '06-confirm-row' | Out-Null
    ClickRow 0 '07-assign' -Wait 0.4
    Assert-Screen 'settings_listen_screen' '07-assign' | Out-Null
    Drive @('shot:08-listening.png', 'key:key_j', "wait:2", 'shot:09-bound.png')
    Assert-Screen 'settings_bind_general_a_screen' '09-bound' | Out-Null
    Assert-Profile 'key:j' $true '09-bound' 'profile-09-after-tap.toml'
    # One Back returns to the page that owns the row: no relaunch, no root re-entry.
    Drive @('key:b', "wait:$StepSeconds", 'shot:10-interface-rows-after.png')
    Assert-Screen 'settings_interface_mapping_screen' '10-interface-rows-after' | Out-Null
    Assert-RowText 'settings_interface_mapping_screen' 'input.general.a = Confirm: G, H, J' '10-interface-rows-after'

    # --- a list too long for the row ---------------------------------------------------------
    Drive @('key:b', "wait:$StepSeconds", 'shot:11-back-controls.png')
    Assert-Screen 'settings_controls_screen' '11-back-controls' | Out-Null
    ClickRow 2 '12-gameplay-mapping'
    Assert-Screen 'settings_gameplay_mapping_screen' '12-gameplay-mapping' | Out-Null
    ClickRow 0 '13-controller'
    Assert-Screen 'settings_gameplay_controller_screen' '13-controller' | Out-Null
    # P4: Custom Mapping lists the three groups (Notes, Move Lane, Pause & Power-up) and Reset All;
    # First Note lives one level down, under Notes.
    ClickRow 1 '14-custom-mapping'
    Assert-Screen 'settings_controller_actions_screen' '14-custom-mapping' | Out-Null
    ClickRow 0 '15-notes'
    Assert-Screen 'settings_controller_notes_screen' '15-notes' | Out-Null
    Drive @("wait:$StepSeconds", 'shot:16-controller-actions.png')
    Assert-RowText 'settings_controller_notes_screen' 'input.controller.first_note = Multiple' '16-controller-actions'
} catch {
    $script:failures += $_.Exception.Message
    Write-Host ("  !! run stopped: {0}" -f $_.Exception.Message)
} finally {
    Start-Sleep -Seconds 2
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500
}

# The overlay's own record of every page's row text, in draw order, beside the shots.
if (Test-Path $log) {
    Select-String -Path $log -Pattern 'help_overlay: binding rows ' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'binding-rows.txt')
    Select-String -Path $log -Pattern 'native_mouse: screen=' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'screens.txt')
}
Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outDir 'profile-final.toml')

Write-Host "`n=== Binding-row acceptance ==="
Write-Host ("steps: {0}, failures: {1}" -f $script:steps, $script:failures.Count)
foreach ($failure in $script:failures) { Write-Host "  - $failure" }
$ok = $script:failures.Count -eq 0
Write-Host ("binding rows: {0}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }))
Write-Host "evidence: $outDir"
if (-not $ok) { exit 1 }
exit 0
