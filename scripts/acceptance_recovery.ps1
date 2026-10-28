# Controls recovery acceptance (docs/plans/finished/2026-10-17-gameplay-input-mapping-plan.md
# §14.4/§17.2, P8a).
#
# A saved profile can leave the menu with no usable input at all: both host gates off and no pad
# connected. Ordinary input is inert then, so the row that would fix it cannot be reached - which
# is the recovery surface the external app provided until P8 retires it. This arm boots exactly
# that state and proves the in-game path:
#
#   * the profile stores keyboard_support = false and mouse_ui_nav = false, and the boot runs with
#     `--input_backend=none`, so there is no pad driver at all;
#   * the caption draws the "hold F10" sentence - with Settings Labels OFF, which is the rule for a
#     navigation problem (the plain row captions stay hidden);
#   * one 2-second hold of F10 reopens the keyboard through the Controls store, logs the need and
#     the outcome, and writes exactly one switch: the profile no longer carries
#     keyboard_support = false (true is the compiled default, so the row is erased) while
#     mouse_ui_nav = false is untouched;
#   * a menu key works afterwards. It cannot be the legacy MnK emulation - that driver is not
#     installed on a boot whose keyboard gate was closed - so this is General Mapping's own key
#     defaults reaching the guest through the synthetic host device the reopened gate brings back.
#
# Usage:  pwsh -File scripts\acceptance_recovery.ps1 [-Label p8a-recovery]
# Writes out/gameplay-mapping/in-game/<Label>/ (log, argv, profile before/after, OCR text) and
# <Label>-shots/*.png, prints a per-step verdict, and exits non-zero unless every step behaved.
param(
    [string]$Label = 'p8a-recovery',
    [string]$BuildDir = 'out/build/win-amd64-release',
    [int]$BootTimeoutSec = 240,
    [int]$TitleTimeoutSec = 180,
    [int]$MenuTimeoutSec = 240
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

# The unusable profile: both host gates off, nothing else. The boot finds it through
# --launcher_profile, applies it at config rank, and no device enumerates.
@"
schema_version = 1

[settings]
keyboard_support = false
mouse_ui_nav = false
"@ | Set-Content -Path $profilePath
@"
schema_version = 1

[general]
target = 'common'
"@ | Set-Content -Path (Join-Path $scratch 'settings.toml')

$exeArgs = @("--game_data_root=$game", '--fullscreen=false',
             "--user_data_root=$scratch", "--launcher_profile=$profilePath",
             "--dlc_library=$empty", "--dlc_root=$empty",
             '--enhancements_dlc_uncap_enumeration=true',
             "--log_file=$log", '--mouse_native_trace=true', '--input_backend=none',
             '--no-settings_help_overlay')
$exeArgs | Set-Content (Join-Path $outDir 'argv.txt')
New-Item -ItemType Directory -Force -Path (Join-Path $root $shots) | Out-Null

$script:failures = @()
$script:steps = 0

function Drive {
    param([string[]]$Actions)
    & (Join-Path $root 'scripts\drive_ui.ps1') -Actions $Actions -OutDir $shots | Out-Null
}

function Assert-True {
    param([bool]$Condition, [string]$Tag, [string]$What)
    $script:steps += 1
    if ($Condition) {
        Write-Host ("  {0}: {1}" -f $Tag, $What)
    } else {
        $script:failures += "${Tag}: ${What}"
        Write-Host ("  !! {0}: {1}" -f $Tag, $What)
    }
}

# Windows.Media.Ocr is only projected in Windows PowerShell, not in pwsh (the probe harness's own
# note), so OCR runs in a child 5.1 process.
function Get-ShotText {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    return (& powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
            (Join-Path $root 'scripts\ocr_image.ps1') -Path $Path 2>$null | Out-String)
}

function Wait-LogPattern {
    param([string]$Pattern, [int]$TimeoutSeconds, [string]$Tag)
    $script:steps += 1
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path $log) {
            if (Select-String -Path $log -Pattern $Pattern -ErrorAction SilentlyContinue) {
                Write-Host ("  {0}: the log matches '{1}'" -f $Tag, $Pattern)
                return $true
            }
        }
        if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) { break }
        Start-Sleep -Milliseconds 500
    }
    $script:failures += "${Tag}: the log never matched '$Pattern'"
    Write-Host ("  !! {0}: the log never matched '{1}'" -f $Tag, $Pattern)
    return $false
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
    Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outDir 'profile-00-boot.toml')

    # The guest's own title screen, read by OCR (a boot with no usable input never leaves it).
    $title = ''
    $deadline = (Get-Date).AddSeconds($TitleTimeoutSec)
    while ((Get-Date) -lt $deadline -and -not ($title -match '(?i)TO START|PRESS')) {
        if (-not (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) { throw 'the game exited before the title was read' }
        Drive @('shot:00-title.png')
        $title = Get-ShotText (Join-Path $root "$shots\00-title.png")
        if (-not ($title -match '(?i)TO START|PRESS')) { Start-Sleep -Seconds 3 }
    }
    $title | Set-Content (Join-Path $outDir 'ocr-00-title.txt')
    Assert-True ($title -match '(?i)TO START|PRESS') '00-title' "the guest title was read: '$($title.Trim())'"

    # What the boot says about the state: the announcement, and the gates it read.
    Wait-LogPattern 'input recovery: no usable input' 60 '01-need' | Out-Null
    Wait-LogPattern 'gameplay mapping: keyboard_support=off \(config\) mouse_ui_nav=off \(config\)' 30 '01-gates' | Out-Null
    Assert-Profile 'keyboard_support = false' $true '01-boot-profile' ''

    # The notice, drawn although Settings Labels is off. The assertion is on the sentence's own
    # words rather than on "F10": the OCR reads the caption's typeface as "Holé 10" here, and a
    # harness that fails on a glyph substitute would report a product defect that is not one.
    Drive @('wait:2', 'shot:02-notice.png')
    $notice = Get-ShotText (Join-Path $root "$shots\02-notice.png")
    $notice | Set-Content (Join-Path $outDir 'ocr-02-notice.txt')
    Assert-True ($notice -match '(?i)no input is available') '02-notice' "the caption reads the recovery sentence: '$($notice.Trim())'"

    # One 2-second hold reopens the keyboard. The harness holds 2.6 s so the release is its own
    # event and the confirmation is read with the key already up.
    Drive @('hold:key_f10:2.6', 'wait:2', 'shot:03-recovered.png')
    Wait-LogPattern 'input recovery: F10 held for 2000 ms with no usable input' 30 '03-reopened' | Out-Null
    Assert-Profile 'keyboard_support = false' $false '03-profile' 'profile-03-after-recovery.toml'
    Assert-Profile 'mouse_ui_nav = false' $true '03-mouse-untouched' ''
    $confirm = Get-ShotText (Join-Path $root "$shots\03-recovered.png")
    $confirm | Set-Content (Join-Path $outDir 'ocr-03-recovered.txt')
    # "switches" appears only in the confirmation, never in the need sentence, and the OCR keeps
    # it intact ("Keyboard $upport is bac140n." is the substituted part).
    Assert-True ($confirm -match '(?i)switch') '03-confirm' "the caption confirms the reopen: '$($confirm.Trim())'"

    # A menu key now reaches the guest. It is General Mapping's own Enter -> A, because the legacy
    # MnK driver was never installed on this boot: the reopened gate brings the synthetic host
    # device back, the state filter runs, and the key path does the rest.
    Drive @('key:key_enter', 'wait:6', 'shot:04-menu.png')
    $menu = Wait-LogPattern 'content_pipeline: usable menu' $MenuTimeoutSec '04-menu-key'
    if (-not $menu) {
        # A slower boot still proves the key arrived if the guest left the title screen.
        $screen = Select-String -Path $log -Pattern 'native_mouse: screen=(?!splash_screen)\S+' -ErrorAction SilentlyContinue
        Assert-True ($null -ne $screen) '04-menu-key-fallback' 'the guest left the title screen after the key'
    }
    Assert-True (-not $process.HasExited) '05-alive' 'the game is still running'
    Drive @('shot:05-after.png')
} catch {
    $script:failures += $_.Exception.Message
    Write-Host ("  !! run stopped: {0}" -f $_.Exception.Message)
} finally {
    Start-Sleep -Seconds 2
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 500
}

if (Test-Path $log) {
    Select-String -Path $log -Pattern 'input recovery:|gameplay mapping: keyboard_support=' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'recovery-lines.txt')
    Select-String -Path $log -Pattern 'native_mouse: screen=' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Line } | Set-Content (Join-Path $outDir 'screens.txt')
}
if (Test-Path $profilePath) {
    Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outDir 'profile-final.toml')
}

Write-Host "`n=== Controls recovery acceptance ==="
Write-Host ("steps: {0}, failures: {1}" -f $script:steps, $script:failures.Count)
foreach ($failure in $script:failures) { Write-Host "  - $failure" }
$ok = $script:failures.Count -eq 0
Write-Host ("recovery: {0}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }))
Write-Host "evidence: $outDir"
if (-not $ok) { exit 1 }
exit 0
