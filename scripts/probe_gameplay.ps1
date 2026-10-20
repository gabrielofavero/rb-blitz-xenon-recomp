<#
P0 gameplay-seam arm (2026-10-17, extended 2026-10-18). Boots the product binary with the P0 probe
enabled, starts a song through the song list, presses native pad controls and/or runs the probe's
timed direct-action sequence, then leaves the combined log and shots.

  pwsh -File scripts\probe_gameplay.ps1 -Label native
  pwsh -File scripts\probe_gameplay.ps1 -Label native-hold -NativeHold
  pwsh -File scripts\probe_gameplay.ps1 -Label inject -Inject
  pwsh -File scripts\probe_gameplay.ps1 -Label track -Inject -InjectTrack
  pwsh -File scripts\probe_gameplay.ps1 -Label bypass -Bypass -Inject
  pwsh -File scripts\probe_gameplay.ps1 -Label block -Block -NativeHold -Inject
  pwsh -File scripts\probe_gameplay.ps1 -Label spam -Spam
  pwsh -File scripts\probe_gameplay.ps1 -Label no-pad -Backend none -NativeHold
  pwsh -File scripts\probe_gameplay.ps1 -Label song-end -SongEnd
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidatePattern("^[a-zA-Z0-9_-]+$")][string]$Label,
    [switch]$Bypass,
    [switch]$Inject,
    [switch]$InjectTrack,
    [switch]$Block,
    [switch]$Spam,
    [switch]$NativeHold,
    [switch]$Pause,
    [switch]$SongEnd,
    [switch]$Dpad,
    [switch]$Ultimate,
    [switch]$Challenge,
    [switch]$Sources,
    # P2's instrument: log every gameplay-table source transition (with the binding that caused
    # it and whether the table was active). One switch per question so an arm never has to guess
    # what was measured.
    [switch]$SourceTrace,
    # P2's mouse-source arm: keep native mouse support ON and give the Keyboard + Mouse table a
    # mouse binding, so a click in a song can be observed as `source=mouse:left`. Without this the
    # harness runs `--no-mouse_ui_nav` as it always has, which is the mouse-gate control.
    [switch]$MouseSource,
    # P2's keyboard-gate arm: turn Keyboard Support off. The legacy MnK emulation driver must not
    # be installed, so the harness's own key-driven pad presses stop reaching the guest and no
    # gameplay source trace line appears.
    [switch]$NoKeyboard,
    # P3's held-track arm: hold Move Lane Right (K) past the controller's own repeat delay and then
    # release it, so a real binding shows both the native repeat it arms and the release that
    # stops it (the plan's D5 held-state rule).
    [switch]$LongTrack,
    [ValidateSet("Song", "Tutorial", "Calibration")][string]$Context = "Song",
    [switch]$KeepRunning,
    [string]$Backend = '',
    [int]$Seconds = 240,
    [int]$SongEndTimeoutSec = 360
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root 'out\build\win-amd64-release'
$exe = Join-Path $work 'rb_blitz.exe'
$game = Join-Path $root 'game'
$base = Join-Path $root 'out\gameplay-mapping\p0'
$outDir = Join-Path $base $Label
$scratch = Join-Path $base "$Label-userdata"
$empty = Join-Path $base 'empty-dlc'
foreach ($p in @($exe, (Join-Path $game 'default.xex'))) {
    if (-not (Test-Path $p)) { throw "missing $p" }
}
if (Get-Process rb_blitz -ErrorAction SilentlyContinue) { throw "Close the running game before starting an isolated probe." }

if ((Test-Path -LiteralPath $outDir) -or (Test-Path -LiteralPath $scratch)) { throw "Use a fresh -Label; probe evidence is never overwritten." }
New-Item -ItemType Directory -Force -Path $outDir, $scratch, $empty | Out-Null
@"
schema_version = 1

[general]
target = 'common'

[graphics]
resolution = 'auto'
"@ | Set-Content -Path (Join-Path $scratch 'settings.toml')
$launcherToml = Join-Path $scratch 'launcher.toml'
Set-Content -Path $launcherToml -Value ''
if ($MouseSource) {
    # The Keyboard + Mouse table's Powerup, with a mouse button beside the key: the click the
    # -Sources arm already sends then has a binding to observe.
    Add-Content -Path $launcherToml -Value @'

[gameplay_remap_keyboard_mouse]
powerup = 'key:Space, mouse:left'
'@
}

$logDir = $outDir

$exeArgs = @("--game_data_root=$game", '--mnk_mode=1', '--fullscreen=false',
             "--user_data_root=$scratch", "--launcher_profile=$launcherToml",
             "--dlc_library=$empty", "--dlc_root=$empty",
             '--enhancements_dlc_cache=true', '--enhancements_dlc_uncap_enumeration=true',
             "--log_file=$(Join-Path $outDir 'game.log')", '--probe_gameplay=true', '--mouse_native_trace=true',
             "--probe_gameplay_bypass=$($Bypass.IsPresent.ToString().ToLower())",
             "--probe_gameplay_inject=$(($Inject -or $Spam).ToString().ToLower())",
             "--probe_gameplay_inject_track=$($InjectTrack.IsPresent.ToString().ToLower())",
             "--probe_gameplay_inject_spam=$($Spam.IsPresent.ToString().ToLower())",
             "--probe_gameplay_block_dispatch=$($Block.IsPresent.ToString().ToLower())",
             "--probe_gameplay_challenge=$($Challenge.IsPresent.ToString().ToLower())",
             "--probe_gameplay_sources=$($Sources.IsPresent.ToString().ToLower())")
# The native pointer is off unless a mouse-source arm asks for it - that is the mouse-gate control.
if (-not $MouseSource) { $exeArgs += '--no-mouse_ui_nav' }
# The SDK spells a false boolean `--no-<cvar>`: the hyphen is the prefix, the cvar keeps its
# underscore. `--no_keyboard_support` is an unknown cvar, not a false switch.
if ($NoKeyboard) { $exeArgs += '--no-keyboard_support' }
if ($Backend) { $exeArgs += "--input_backend=$Backend" }
# The source trace travels through the environment (the SDK's own `REX_` + uppercase spelling), so
# the arm does not depend on how the command line parses a trailing bool.
if ($SourceTrace) { $env:REX_INPUT_SOURCE_TRACE = 'true' } else { Remove-Item Env:REX_INPUT_SOURCE_TRACE -ErrorAction SilentlyContinue }
if ($Ultimate) {
    # The verified payload boot (docs/guides/ultimate-compat.md): mode 2 mounts
    # the content device. ultimate_patches keeps its default (all bits).
    $exeArgs += @('--ultimate_mode=2', "--ultimate_payload_root=$(Join-Path $game 'ultimate')")
}
if ($Dpad) {
    # The D-pad's default binds are Shift+arrow, which one keybd_event cannot
    # send; rebind the four directions to plain letters instead.
    $exeArgs += @('--keybind_dpad_up=G', '--keybind_dpad_down=H',
                  '--keybind_dpad_left=N', '--keybind_dpad_right=M')
}
Write-Host ("launch: rb_blitz.exe {0}" -f ($exeArgs -join ' '))
$exeArgs | Set-Content (Join-Path $outDir "argv.txt")
$process = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $exeArgs
$process.Id | Set-Content (Join-Path $outDir 'pid.txt')

$shots = Join-Path $base "$Label-shots"
$relativeShots = "out/gameplay-mapping/p0/$Label-shots"
if (Test-Path -LiteralPath $shots) { throw "Shot directory already exists." }
New-Item -ItemType Directory -Force -Path $shots | Out-Null
$deadline = (Get-Date).AddSeconds($Seconds)
$reached = $false
while ((Get-Date) -lt $deadline) {
    if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) { throw 'the game exited early' }
    $logs = Get-ChildItem $logDir -Filter '*.log' -ErrorAction SilentlyContinue
    if ($logs -and (Select-String -Path $logs.FullName -Pattern 'settings_boot: presented menu frame' -ErrorAction SilentlyContinue)) {
        $reached = $true
        break
    }
    Start-Sleep -Milliseconds 750
}
if (-not $reached) { Write-Warning 'the menu frame was never announced; driving anyway' }

# The guest's own title must be up before any key is sent. Input during the boot or the content
# discovery is what the known GPU packet desync was first seen with (docs/reference/known-issues.md),
# and the Ultimate payload boots slowly enough that a fixed wait lands inside it.
# Windows.Media.Ocr is only projected in Windows PowerShell, not in pwsh.
function Wait-GuestTitle([int]$TimeoutSeconds = 180) {
    $capture = Join-Path $outDir 'wait-title.png'
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) { return $false }
        try { & (Join-Path $root 'scripts\capture_window.ps1') -OutFile $capture | Out-Null } catch { }
        $text = ''
        if (Test-Path -LiteralPath $capture) {
            $text = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
                (Join-Path $root 'scripts\ocr_image.ps1') -Path $capture 2>$null | Out-String
        }
        if ($text -match '(?i)TO START|PRESS') {
            Write-Host 'guest title reached'
            return $true
        }
        Start-Sleep -Seconds 3
    }
    Write-Warning 'the guest title was never read; driving anyway'
    return $false
}
if (-not (Wait-GuestTitle)) { Write-Warning 'continuing without the title confirmation' }

try {
    $up = @('key:lstick_up', 'wait:1', 'key:lstick_up', 'wait:1', 'key:lstick_up', 'wait:1',
            'key:lstick_up', 'wait:1', 'key:lstick_up', 'wait:1', 'key:lstick_up', 'wait:1')
    $down = @('key:lstick_down', 'wait:1', 'key:lstick_down', 'wait:1')
    $actions = @('wait:10', 'key:a', 'wait:4', 'key:a', 'wait:10', "shot:00-songs.png") + $up + $down +
               @('shot:01-selected.png', 'key:a', 'wait:12', "shot:02-load.png",
                 'key:a', 'wait:20', "shot:03-gameplay.png",
                 'key:a', 'wait:1',
                 'wait:10', "shot:04-after.png")
    if ($Sources) {
        # G/H/N/M were the P0 probe's own source letters (unbound in the product's tables);
        # J/K/D and Space (`key:a` sends Space) are the Keyboard + Mouse table's own defaults, so
        # `--input_source_trace` has something real to report. The bound keys are also *held* for
        # two seconds, because the sampler's heartbeat is one line per ~2.5 s and a 90 ms tap can
        # fall between two heartbeats even when the key is really down. Every tap has its own
        # wait: the guest drops a run of taps with no wait between them.
        $actions += @('key:key_g', 'wait:1', 'key:key_h', 'wait:1',
                      'hold:key_j:2', 'wait:1', 'hold:key_k:2', 'wait:1', 'hold:key_d:2', 'wait:1',
                      'hold:a:2', 'wait:1',
                      'mouse:0.5:0.5', 'click:left', 'wait:1', 'click:right', 'wait:1',
                      'hold:key_n:6', 'wait:1', 'hold:key_m:6', 'wait:1', 'shot:10-sources.png')
    }
    if ($Context -ne 'Song') {
        # Fresh profile: the main menu clamps to PLAY, and three rows down from there is
        # HELP & OPTIONS. Inside the merged root the rows are General, Controls, Calibration,
        # Audio/Video (Gameplay with Ultimate), Interface. Every tap needs its own wait:1 -
        # the guest drops a run of taps with no wait between them (docs/reference/known-issues.md).
        # Screenshots at each level so a changed row order is visible rather than silently accepted.
        $actions = @('wait:10', 'key:a', 'wait:5', 'shot:00-main.png') + $up +
                   @('key:lstick_down', 'wait:1', 'key:lstick_down', 'wait:1',
                     'key:lstick_down', 'wait:1', 'key:a', 'wait:4',
                     'shot:01-help.png') + $up
        if ($Context -eq 'Tutorial') {
            # General is the root's first row; two rows down inside it is How to Play
            # (the menu keeps a remembered selection, so the count is calibrated by the shots).
            $actions += @('key:a', 'wait:4', 'shot:02-general.png')
            $actions += ('key:lstick_down', 'wait:1') * 2
            $actions += @('key:a', 'wait:10', 'shot:03-tutorial.png')
        } else {
            # Calibration is the root's third row.
            $actions += @('key:lstick_down', 'wait:1', 'key:lstick_down', 'wait:1',
                          'key:a', 'wait:6', 'shot:03-calibration.png')
        }
        # The measurement this arm exists for: the same physical controls that map to gameplay
        # actions must produce no dispatch inside a menu context.
        $actions += @('key:key_g', 'wait:1', 'key:key_h', 'wait:1', 'key:x', 'wait:1',
                      'key:trigger_r', 'wait:1', 'shot:04-context-keys.png')
    }
    if ($NativeHold) {
        # Each hold names the producer it exercises: the stick direction and the
        # trigger reach the boundary through the symbol message producer, the
        # buttons through ButtonDown. trigger_r is held past the repeat delay so
        # the controller's own repeat fires, then released to stop it.
        $actions += @('hold:lstick_up:0.4', 'wait:1',
                      'hold:x:0.4', 'wait:1',
                      'hold:shoulder_r:0.4', 'wait:1',
                      'hold:shoulder_l:0.4', 'wait:1',
                      'hold:trigger_l:0.4', 'wait:1',
                      'hold:trigger_r:7.0', 'wait:2', "shot:06-native-hold.png")
    }
    if ($Spam) {
        # The probe starts its alternating Lane0/Lane1 loop about 8 s after the
        # song controller exists, so 05 is the zero-score reference and 06/07
        # show the judged notes.
        $actions += @('wait:25', "shot:05-score-a.png", 'wait:15', "shot:06-score-b.png",
                      'wait:15', "shot:07-score-c.png")
    }
    if ($Dpad) {
        # Each direction is its own kPad id (12-15) but the same preset action
        # (Lane0 on the shipped preset).
        $actions += @('hold:key_g:0.4', 'wait:1', 'hold:key_h:0.4', 'wait:1',
                      'hold:key_n:0.4', 'wait:1', 'hold:key_m:0.4', 'wait:1',
                      "shot:09-dpad.png")
    }
    if ($Pause) { $actions += @('key:esc', 'wait:3', 'shot:05-pause.png', 'key:esc', 'wait:3', 'shot:11-resume.png') }
    if ($LongTrack) {
        # The controller's own repeat starts about 5 s after a track press (P0's `track` arm), so
        # the hold has to outlast that; the release must then stop it (sub_824367E0 clears the
        # held-action slot). Move Lane Left is held second so both directions are covered.
        $actions += @('hold:key_k:13', 'wait:2', "shot:12-track-right.png",
                      'hold:key_d:13', 'wait:2', "shot:13-track-left.png", 'wait:3',
                      "shot:14-track-stopped.png")
    }
    & (Join-Path $root 'scripts\drive_ui.ps1') -Actions $actions -OutDir $relativeShots

    if ($SongEnd) {
        # Wait for the guest to tear the song controller down (the probe's
        # destructor hook), then photograph the results screen.
        $deadline = (Get-Date).AddSeconds($SongEndTimeoutSec)
        $ended = $false
        while ((Get-Date) -lt $deadline) {
            $logs = Get-ChildItem $logDir -Filter '*.log' -ErrorAction SilentlyContinue
            if ($logs -and (Select-String -Path $logs.FullName -Pattern 'gameplay_probe: controller destroyed' -ErrorAction SilentlyContinue)) {
                $ended = $true
                break
            }
            if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Seconds 2
        }
        Write-Host ("song end observed: {0}" -f $ended)
        Start-Sleep -Seconds 4
        & (Join-Path $root 'scripts\drive_ui.ps1') -Actions @("shot:08-song-end.png") -OutDir $relativeShots
    }
} finally {
    Start-Sleep -Seconds 2
    if (-not $KeepRunning -and -not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }

}

$combined = Join-Path $outDir "$Label.log"
Get-ChildItem $logDir -Filter '*.log' -ErrorAction SilentlyContinue | Sort-Object Name |
    ForEach-Object { Get-Content $_.FullName } | Set-Content -Path $combined
Write-Host "shots: $shots"
Write-Host "log:   $combined"
Select-String -Path $combined -Pattern 'gameplay_probe: (challenge|track slot|source=|controller destroyed)|blocked native|input source:|input sources:|title_input:' | Select-Object -First 60 | ForEach-Object { $_.Line }
