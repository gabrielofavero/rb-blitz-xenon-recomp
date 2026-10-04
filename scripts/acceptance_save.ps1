# Save/load acceptance: a setting changed in the game survives a restart.
#
# The persistence acceptance (scripts/acceptance_persistence.ps1) proves the
# storage layer: the title authors its save files into an isolated
# --user_data_root, reads them back on a second process, and copes with a missing
# or corrupt one. What it does not prove is the user-visible round trip the alpha
# needs: *change a setting, quit, come back, and find it still changed*. This
# drives that through the menus, because the guest's own options UI is the only
# thing that writes a setting.
#
# The setting is the manual calibration offset (HELP & OPTIONS -> Calibration ->
# Calibrate Manually): the audio page moves the value in 5 ms steps with the left
# stick, A advances to the video page, A again shows a confirmation
# ("AUDIO OFFSET: -N MS / VIDEO OFFSET: .. MS / CONTINUE / BACK"), and A on
# CONTINUE is the commit - it rewrites `globaloptions` (measured: a
# [NtCreateFile] disp=0x5 + a 1024-byte [NtWriteFile] on the same handle, and the
# file's SHA-256 changes). That commit is the "save".
#
# The AV toggles (Overscan, Bass Boost) and the controller scheme change on screen
# but do *not* rewrite the save file - measured 2026-10-03 across stick, D-pad, A,
# START, B, X, Y, a 30 s wait and a main-menu round trip, with `--log_noisy=true`
# and no [NtWriteFile] on the content handle. Calibration is therefore the setting
# this test uses.
#
# Three boots over one isolated root:
#
#   change   fresh root, calibration set to -N MS and committed. The save file's
#            hash must change, and the [NtWriteFile] for it must be in the log.
#   reload   same root. The calibration page must read back -N MS, and the file
#            must be byte-identical to what `change` left. Then the value is set
#            back to 0 MS and committed again (the revert), which must restore the
#            original bytes.
#   revert   same root. The value must read 0 MS again.
#
# Usage:
#   .\scripts\acceptance_save.ps1
#   .\scripts\acceptance_save.ps1 -Steps 3        # -15 MS instead of -10 MS
#
# Writes out/m7-save/{change,reload,revert}-*.png, a copy of each run's log and
# summary.json, prints a per-leg verdict and exits non-zero unless every leg held.
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m7-save",
    [string]$GameRoot,
    [string]$UltimateMode = "0",
    # Number of left-stick taps on the audio page; each is -5 ms.
    [int]$Steps = 2,
    [int]$BootTimeoutSec = 180,
    [int]$ScreenTimeoutSec = 60,
    [int]$CloseWaitSec = 20
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
# -BuildDir may be absolute (an installed folder outside the repo) or relative to
# the repository root; Join-Path does not resolve an absolute child, so test first.
$work = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$exe = Join-Path $work "rb_blitz.exe"
$logs = Join-Path $work "logs"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
$capDir = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
$userData = Join-Path $capDir "userdata"
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }

# The content the title writes, derived the same way acceptance_persistence.ps1
# does: the SDK's hardcoded local XUID, Rock Band Blitz's title id and the save
# content type 1.
$settingsRel = "B13EBABEBABEBABE\5841122D\00000001\globaloptions\globaloptions"
$settingsFile = Join-Path $userData $settingsRel

# The expected offset after $Steps taps (each -5 ms) and after the revert.
$changed = -5 * $Steps

# ---------------------------------------------------------------- helpers ---

# Anything that runs another .ps1 goes through a child PowerShell: this machine's
# execution policy is Restricted (docs/build-and-run.md §0).
function Invoke-ChildScript([string]$Script, [hashtable]$Named) {
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) { $argv += @("-$k", [string]$Named[$k]) }
    $out = & powershell @argv 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$Script failed: $out" }
    return $out
}

function Get-NewestLog {
    $f = Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $f) { return $null }
    return $f.FullName
}

function Get-LogText([string]$Path) {
    if (-not $Path -or -not (Test-Path $Path)) { return "" }
    for ($i = 0; $i -lt 5; $i++) {
        try { return (Get-Content -LiteralPath $Path -Raw) } catch { Start-Sleep -Milliseconds 200 }
    }
    return ""
}

function Save-Shot([string]$Name) {
    $path = Join-Path $capDir $Name
    for ($i = 1; $i -le 3; $i++) {
        try { Invoke-ChildScript "capture_window.ps1" @{ OutFile = $path } | Out-Null; return $path }
        catch { if ($i -eq 3) { throw }; Start-Sleep -Seconds 2 }
    }
}

function Get-ScreenText([string]$Shot) {
    return ((Invoke-ChildScript "ocr_image.ps1" @{ Path = $Shot }) -join " ").ToUpperInvariant()
}

function Wait-ForScreen([string]$Shot, [string]$Needle, [int]$TimeoutSec, [string]$Label) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = ""
    while ((Get-Date) -lt $deadline) {
        try { Save-Shot (Split-Path -Leaf $Shot) | Out-Null; $last = Get-ScreenText $Shot }
        catch { Start-Sleep -Seconds 3; continue }
        if ($Needle -eq "" -or $last.Contains($Needle.ToUpperInvariant())) { return $last }
        Start-Sleep -Seconds 3
    }
    Write-Host "  !! timed out after ${TimeoutSec}s waiting for '$Needle' ($Label); last: $last"
    return $last
}

function Wait-ForWindow($Proc, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $Proc.Refresh()
            if ($Proc.HasExited) { return $false }
            if ($Proc.MainWindowHandle -ne [IntPtr]::Zero) { return $true }
        } catch { }
        Start-Sleep -Seconds 2
    }
    return $false
}

function Invoke-Actions([string[]]$Actions) {
    Invoke-ChildScript "drive_ui.ps1" @{ Actions = ($Actions -join ","); BuildDir = $BuildDir; OutDir = $OutDir } | Out-Null
}

# Every list clamps at its first row (measured; see scripts/acceptance_screens.ps1),
# so walking up past the top fixes the highlight before a counted number of taps.
function Reset-Highlight {
    $actions = @()
    for ($i = 0; $i -lt 8; $i++) { $actions += "key:lstick_up" }
    Invoke-Actions $actions
}

function Get-SettingHash {
    if (-not (Test-Path $settingsFile)) { return "" }
    return (Get-FileHash -LiteralPath $settingsFile -Algorithm SHA256).Hash
}

# "CURRENT OFFSET: -10 MS" (calibration page) or "AUDIO OFFSET: -10 MS" (the
# confirmation screen) -> -10. The Windows OCR engine reads the game's digits as
# letters often enough to matter here (measured: "0" -> "O", "-10" -> "-IO"), so
# the token is normalised before it is parsed, and null is returned rather than a
# wrong number.
function Get-Offset([string]$Text) {
    $flat = $Text -replace "\s", ""
    $m = [regex]::Match($flat, "(?:CURRENT|AUDIO)OFFSET:?(-?[0-9ODIL|ZSBGQ]+)MS")
    if (-not $m.Success) { return $null }
    $token = $m.Groups[1].Value
    $map = @{ "O" = "0"; "D" = "0"; "I" = "1"; "L" = "1"; "|" = "1"; "Z" = "2"; "S" = "5"; "B" = "8"; "G" = "9"; "Q" = "9" }
    $digits = -join ($token.ToCharArray() | ForEach-Object { if ($map.ContainsKey([string]$_)) { $map[[string]$_] } else { $_ } })
    return [int]$digits
}

# Boot -> three dialogs (sign-in, offline prompt, title) -> main menu.
function Start-ToMenu($Proc, [string]$Tag) {
    if (-not (Wait-ForWindow $Proc $BootTimeoutSec)) { throw "$Tag : no game window appeared" }
    Start-Sleep -Seconds 20
    Invoke-Actions @("key:a", "wait:6", "key:a", "wait:6", "key:a", "wait:8")
}

# main menu -> HELP & OPTIONS -> Calibration -> Calibrate Manually.
function Open-ManualCalibration([string]$Tag) {
    Reset-Highlight
    Invoke-Actions @("key:lstick_down", "key:lstick_down", "key:lstick_down", "wait:1", "key:a", "wait:6")
    Reset-Highlight
    Invoke-Actions @("key:lstick_down", "key:lstick_down", "wait:1", "key:a", "wait:6")
    Reset-Highlight
    Invoke-Actions @("key:lstick_down", "wait:1", "key:a", "wait:6")
    $shot = Save-Shot "$Tag-manual.png"
    return Get-ScreenText $shot
}

# Audio page -> video page -> confirmation screen. Returns the confirmation text.
function Reach-CalibrationConfirm([string[]]$AudioTaps, [string]$Tag) {
    $actions = @()
    for ($i = 0; $i -lt $AudioTaps.Count; $i++) { $actions += "key:$($AudioTaps[$i])" }
    $actions += @("wait:2", "key:a", "wait:3", "key:a", "wait:3")
    Invoke-Actions $actions
    $shot = Save-Shot "$Tag-confirm.png"
    return Get-ScreenText $shot
}

# ---------------------------------------------------------------- one leg ---

function Invoke-Leg([string]$Name, [scriptblock]$Body) {
    Write-Host "=== $Name ==="
    Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue | Remove-Item -Force
    $notes = New-Object System.Collections.ArrayList
    $row = [ordered]@{
        Leg = $Name; Menu = $false; StartOffset = $null; CommitOffset = $null
        ReadbackOffset = $null; RevertOffset = $null
        HashBefore = ""; HashAfterChange = ""; HashAfterReload = ""; HashAfterRevert = ""
        CommitWrite = $false; Fatal = $false; Notes = ""
    }
    $proc = $null
    try {
        $proc = & $Body $row $notes
    } catch {
        $notes.Add("$($_.Exception.Message)") | Out-Null
    } finally {
        if ($proc -and -not $proc.HasExited) {
            try {
                $proc.CloseMainWindow() | Out-Null
                if (-not $proc.WaitForExit($CloseWaitSec * 1000)) { Stop-Process -Id $proc.Id -Force }
            } catch { }
        }
    }
    # A copy of the run log and the write evidence, so the verdict can be checked
    # without re-running.
    $log = Get-NewestLog
    $text = Get-LogText $log
    $row.Fatal = [bool]($text | Select-String -Pattern "\[FATAL\]" -Quiet)
    if ($log) { Copy-Item $log (Join-Path $capDir "$Name.log") -Force }
    $row.CommitWrite = [bool]($text | Select-String -Pattern "path=globaloptions:\\globaloptions.*disp=0x5" -Quiet)
    $row.Notes = ($notes -join "; ")
    Write-Host ("  start={0} commit={1} readback={2} revert={3} hash {4} -> {5}" -f `
        $row.StartOffset, $row.CommitOffset, $row.ReadbackOffset, $row.RevertOffset, `
        $row.HashBefore.Substring(0, [Math]::Min(12, $row.HashBefore.Length)), `
        $row.HashAfterChange.Substring(0, [Math]::Min(12, $row.HashAfterChange.Length)))
    return [pscustomobject]$row
}

function New-Process([string]$UserRoot) {
    $argv = @(
        "--game_data_root=$GameRoot", "--ultimate_mode=$UltimateMode", "--mnk_mode=1",
        "--no_mouse_ui_nav", "--user_data_root=$UserRoot",
        "--log_noisy=true", "--log_level=trace", "--log_flush_interval=1", "--log_max_file_size_mb=200"
    )
    return Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $argv
}

# ------------------------------------------------------------------- main ---

if (Test-Path $userData) { Remove-Item -Recurse -Force $userData }
New-Item -ItemType Directory -Force -Path $userData | Out-Null

$change = Invoke-Leg "change" {
    param($row, $notes)
    $proc = New-Process $userData
    Start-ToMenu $proc "change"
    $text = Open-ManualCalibration "change"
    $row.Menu = $text.Contains("OFFSET")
    $row.StartOffset = Get-Offset $text
    if ($row.StartOffset -ne 0) { $notes.Add("a fresh root did not start at 0 MS (read $($row.StartOffset))") | Out-Null }
    $row.HashBefore = Get-SettingHash
    $taps = @(); for ($i = 0; $i -lt $Steps; $i++) { $taps += "lstick_left" }
    $confirm = Reach-CalibrationConfirm $taps "change"
    $row.CommitOffset = Get-Offset $confirm
    if ($row.CommitOffset -ne $changed) { $notes.Add("the confirmation screen reads '$($row.CommitOffset)' MS, expected $changed") | Out-Null }
    Invoke-Actions @("key:a", "wait:4")   # A on CONTINUE: the commit
    $row.HashAfterChange = Get-SettingHash
    if ($row.HashAfterChange -eq $row.HashBefore) { $notes.Add("committing the calibration did not change $settingsRel") | Out-Null }
    return $proc
}

$reload = Invoke-Leg "reload" {
    param($row, $notes)
    if (-not $change.HashAfterChange) { throw "the change leg did not produce a save file" }
    $row.HashBefore = $change.HashAfterChange
    $proc = New-Process $userData
    Start-ToMenu $proc "reload"
    $text = Open-ManualCalibration "reload"
    $row.Menu = $text.Contains("OFFSET")
    $row.ReadbackOffset = Get-Offset $text
    if ($row.ReadbackOffset -ne $changed) { $notes.Add("the reloaded calibration reads '$($row.ReadbackOffset)' MS, expected $changed - the setting did not persist") | Out-Null }
    $row.HashAfterReload = Get-SettingHash
    if ($row.HashAfterReload -ne $row.HashBefore) { $notes.Add("the reload rewrote the save file") | Out-Null }
    # Revert to 0 MS and commit, so the machine is left as it was found.
    $taps = @(); for ($i = 0; $i -lt $Steps; $i++) { $taps += "lstick_right" }
    $confirm = Reach-CalibrationConfirm $taps "reload"
    $row.RevertOffset = Get-Offset $confirm
    Invoke-Actions @("key:a", "wait:4")
    $row.HashAfterRevert = Get-SettingHash
    return $proc
}

$revert = Invoke-Leg "revert" {
    param($row, $notes)
    $proc = New-Process $userData
    Start-ToMenu $proc "revert"
    $text = Open-ManualCalibration "revert"
    $row.Menu = $text.Contains("OFFSET")
    $row.ReadbackOffset = Get-Offset $text
    if ($row.ReadbackOffset -ne 0) { $notes.Add("after the revert the calibration reads '$($row.ReadbackOffset)' MS, expected 0") | Out-Null }
    $row.HashAfterRevert = Get-SettingHash
    return $proc
}

# ----------------------------------------------------------------- verdict ---

$okChange = $change.Menu -and $change.StartOffset -eq 0 -and $change.CommitOffset -eq $changed -and
            $change.HashBefore -ne "" -and $change.HashAfterChange -ne $change.HashBefore -and
            $change.CommitWrite -and -not $change.Fatal
$okReload = $reload.Menu -and $reload.ReadbackOffset -eq $changed -and
            $reload.HashAfterReload -eq $change.HashAfterChange -and
            $reload.RevertOffset -eq 0 -and -not $reload.Fatal
$okRevert = $revert.Menu -and $revert.ReadbackOffset -eq 0 -and -not $revert.Fatal

$summary = [pscustomobject]@{
    Steps = $Steps; ExpectedMs = $changed
    Change = $okChange; Reload = $okReload; Revert = $okRevert
    HashOriginal = $change.HashBefore; HashChanged = $change.HashAfterChange
    HashReloaded = $reload.HashAfterReload; HashReverted = $reload.HashAfterRevert
    SaveFile = $settingsRel
    ChangeLeg = $change; ReloadLeg = $reload; RevertLeg = $revert
}

Write-Host "`n=== save acceptance ==="
[pscustomobject]@{ Change = $okChange; Reload = $okReload; Revert = $okRevert; Steps = $Steps; ExpectedMs = $changed } |
    Format-List | Out-String | Write-Host
foreach ($leg in @($change, $reload, $revert)) { if ($leg.Notes) { Write-Host ("{0}: {1}" -f $leg.Leg, $leg.Notes) } }
$summary | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $capDir "summary.json")

$ok = $okChange -and $okReload -and $okRevert
Write-Host ("save: {0}" -f $(if ($ok) { "PASS" } else { "FAIL" }))
if (-not $ok) { exit 1 }
exit 0
