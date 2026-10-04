# Observe one named UI state: drive to it, capture it, read it, and say what
# changed. See docs/engine/observing.md for the operator's view.
#
# This is the harness the customization plan's S2-S7 and E1 measure with
# (docs/plans/customization-plan.md, contracts 3 and 5). It joins the scripts
# that already exist rather than re-implementing any of them:
#
#   scripts/drive_ui.ps1      sends the keys (the MnK pad, one row per tap)
#   scripts/capture_window.ps1  saves the window, and refuses to save the wrong one
#   scripts/ocr_image.ps1     reads the static text back
#   scripts/frame_diff.ps1    measures the difference against a baseline
#
# and scripts/ui_states.ps1 is the one table of state names, routes and needles.
#
# Usage:
#   .\scripts\observe_ui.ps1 -ListStates
#   .\scripts\observe_ui.ps1 -State "main menu" -Ocr
#   .\scripts\observe_ui.ps1 -State title -BaselineDir out/observations/baseline
#   .\scripts\observe_ui.ps1 -State title -Compare out/observations/baseline
#
# Two rules it enforces rather than documents:
#
#   * **A pixel diff is only evidence against a same-build baseline.** Without
#     -BaselineDir there is nothing to compare, and -Compare with no baseline
#     file for the state reports `no-baseline` and does NOT fail the run - a
#     measurement with nothing to measure against is not a pass (docs/backlog.md,
#     "Measure the noise floor before believing any screenshot diff").
#   * **The comparison is against the state's settled frame.** The route's own
#     waits are not enough: a screen is only comparable once it has stopped
#     animating, which is why every state carries a settle time and why the
#     default threshold sits between the settled (~0.14 %) and just-entered
#     (3.5-5.9 %) noise floors.
#
# Vanilla by default, like the acceptance runs: an installed Ultimate payload
# must not be able to change what a state looks like. -UltimateMode 1 opts in.
#
# Exits 0 when the state was reached and its expectation held, 1 otherwise. The
# JSON report goes to stdout.
param(
    [string]$State,
    [switch]$ListStates,
    [switch]$Ocr,
    # A directory holding "<state>.png" and, when it was written by
    # -BaselineDir, "<state>.json".
    [string]$Compare,
    # When set, this run becomes the baseline for the state: its capture and a
    # metadata file (build hash, capture hash, OCR text) are written here.
    [string]$BaselineDir,
    [string]$OutDir = "out/observations",
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$GameRoot,
    [string]$UltimateMode = "0",
    # Attach to a running rb_blitz instead of starting one. The route still runs,
    # so the instance has to be freshly booted.
    [switch]$ReuseRunning,
    [switch]$KeepRunning,
    # -1 keeps the state's own settle time.
    [int]$SettleSeconds = -1,
    # Percentage of pixels that may differ from the baseline, unless the state
    # names its own ceiling (ui_states.ps1). The default sits between the settled
    # noise floor (~0.14 %) and the just-entered one (3.5-5.9 %), so it fails on a
    # real change and passes on an unsettled screen - except on a screen whose
    # background animates, which is why those carry a measured per-state ceiling.
    [double]$MaxDiffPercent = 1.0,
    # The game's own default is a fullscreen window, which captures the guest's
    # output with no window chrome in it - the cleanest thing to compare, and what
    # the acceptance runs use. -Windowed asks for a desktop window instead; the
    # capture then has the frame and title bar in it, which is one more thing that
    # can differ between two runs. The capture's actual size is in the report
    # either way, because a capture at one size cannot be diffed against another.
    [switch]$Windowed,
    [int]$BootTimeoutSec = 180
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "ui_states.ps1")

function Invoke-Child([string]$Script, [hashtable]$Named) {
    # This machine's execution policy is Restricted (docs/build-and-run.md §0), so
    # every sibling script goes through a child PowerShell - the same reason and the
    # same shape as scripts/acceptance_screens.ps1.
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) { $argv += @("-$k", [string]$Named[$k]) }
    $out = & powershell @argv 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$Script failed: $out" }
    return $out
}

function Invoke-Actions([string[]]$Actions) {
    if ($Actions.Count -eq 0) { return }
    Invoke-Child "drive_ui.ps1" @{
        Actions = ($Actions -join ","); BuildDir = $BuildDir; OutDir = $OutDir
    } | Out-Null
}

function Get-ScreenText([hashtable]$OcrArgs) {
    return ((Invoke-Child "ocr_image.ps1" $OcrArgs) -join " ").ToUpperInvariant()
}

function Get-Hash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return "" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Resolve-UnderRoot([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
    return (Join-Path $root $Path)
}

function Wait-ForWindow($Proc, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $Proc.Refresh()
            if ($Proc.HasExited) { return $false }
            if ($Proc.MainWindowHandle -ne [IntPtr]::Zero) { return $true }
        } catch { return $false }
        Start-Sleep -Seconds 2
    }
    return $false
}

# --------------------------------------------------------------- list mode ---

if ($ListStates) {
    $list = @()
    foreach ($name in $script:UiStateOrder) {
        $s = $script:UiStates[$name]
        $list += [ordered]@{
            state          = $name
            needle         = $s.Needle
            settle_seconds = $s.Settle
            route          = @($s.Route)
            note           = $s.Note
        }
    }
    $list | ConvertTo-Json -Depth 5
    exit 0
}

if (-not $State) { throw "-State is required (or -ListStates to see the names)" }
# "main menu" and "main-menu" are the same state: the plan writes the names with
# spaces, the table keys them with dashes.
$State = ($State.Trim() -replace "\s+", "-").ToLowerInvariant()
$spec = $script:UiStates[$State]
if (-not $spec) {
    throw "unknown state '$State'; known states: $($script:UiStateOrder -join ', ')"
}

# ---------------------------------------------------------------- the game ---

$work = Join-Path $root $BuildDir
$exe = Join-Path $work "rb_blitz.exe"
if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }

$proc = $null
$launched = $false
$running = Get-Process rb_blitz -ErrorAction SilentlyContinue | Select-Object -First 1
if ($running) {
    if (-not $ReuseRunning) {
        throw "rb_blitz is already running (pid $($running.Id)); close it, or pass -ReuseRunning"
    }
    $proc = $running
} else {
    # --no-mouse_ui_nav: the mouse is a second synthetic pad on the same guest
    # slot, and where the pointer happens to rest must not decide what a capture
    # shows (scripts/acceptance_screens.ps1 turns it off for the same reason).
    $launchArgs = @("--game_data_root=$GameRoot", "--ultimate_mode=$UltimateMode", "--no-mouse_ui_nav")
    if ($Windowed) { $launchArgs += "--fullscreen=0" }
    $proc = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $launchArgs
    $launched = $true
    if (-not (Wait-ForWindow $proc $BootTimeoutSec)) {
        throw "rb_blitz did not open a window within ${BootTimeoutSec}s"
    }
}

$report = [ordered]@{
    state          = $State
    note           = $spec.Note
    ultimate_mode  = $UltimateMode
    binary_sha256  = Get-Hash $exe
    launched       = $launched
    window         = $(if ($Windowed) { "desktop window (capture includes the window chrome)" } else { "fullscreen (capture is the guest output and nothing else)" })
    route          = @($spec.Route)
    capture        = ""
    settle_seconds = 0
    ocr            = [ordered]@{ ran = $false; needle = $spec.Needle; crop = [string]$spec.Crop; matched = $null; text = "" }
    diff           = [ordered]@{ status = "not-requested" }
    passed         = $false
}

try {
    Invoke-Actions @($spec.Route)

    $settle = if ($SettleSeconds -ge 0) { $SettleSeconds } else { [int]$spec.Settle }
    $report.settle_seconds = $settle
    if ($settle -gt 0) { Start-Sleep -Seconds $settle }

    $capDir = Resolve-UnderRoot $OutDir
    New-Item -ItemType Directory -Force -Path $capDir | Out-Null
    $capture = Join-Path $capDir "$State.png"
    Invoke-Child "capture_window.ps1" @{ OutFile = $capture } | Out-Null
    $report.capture = $capture
    # The capture's own size, so a run whose window came out different from the
    # baseline's is visible in the report instead of silently breaking the diff.
    Add-Type -AssemblyName System.Drawing
    $img = [System.Drawing.Image]::FromFile($capture)
    $report["capture_size"] = "$($img.Width)x$($img.Height)"
    $img.Dispose()

    $needle = ([string]$spec.Needle).ToUpperInvariant()
    $crop = [string]$spec.Crop
    $screenOk = $true
    if ($Ocr -or $needle) {
        # Some screens only give up their title in a band (ui_states.ps1 records
        # which and why); the report says when the text is a crop rather than the
        # whole frame, so a crop's text is never mistaken for the screen's.
        $ocrArgs = @{ Path = $capture }
        if ($crop) { $ocrArgs["Crop"] = $crop }
        $text = Get-ScreenText $ocrArgs
        $report.ocr.ran = $true
        $report.ocr.text = $text
        $report.ocr.crop = $crop
        if ($needle) {
            $screenOk = $text.Contains($needle)
            $report.ocr.matched = $screenOk
        }
    }

    if ($BaselineDir) {
        $bd = Resolve-UnderRoot $BaselineDir
        New-Item -ItemType Directory -Force -Path $bd | Out-Null
        $basePng = Join-Path $bd "$State.png"
        Copy-Item -LiteralPath $capture -Destination $basePng -Force
        $meta = [ordered]@{
            state          = $State
            captured_at    = (Get-Date).ToString("o")
            binary_sha256  = $report.binary_sha256
            capture_sha256 = Get-Hash $basePng
            ultimate_mode  = $UltimateMode
            needle         = $spec.Needle
            ocr_text       = $report.ocr.text
        }
        Set-Content -LiteralPath (Join-Path $bd "$State.json") `
            -Value ($meta | ConvertTo-Json -Depth 4) -Encoding UTF8
        $report["baseline_written"] = $basePng
    }

    if ($Compare) {
        $cd = Resolve-UnderRoot $Compare
        $basePng = Join-Path $cd "$State.png"
        # The state's own ceiling wins over the global one; the report says which
        # was used, because "passed at 2.5 %" and "passed at 1 %" are different
        # claims about the same number.
        $threshold = if ([double]$spec.MaxDiff -gt 0) { [double]$spec.MaxDiff } else { $MaxDiffPercent }
        $thresholdSource = if ([double]$spec.MaxDiff -gt 0) { "state" } else { "default" }
        if (-not (Test-Path -LiteralPath $basePng)) {
            # Not a failure: there is nothing to compare against, and saying so is
            # the point (see the header).
            $report.diff = [ordered]@{
                status = "no-baseline"; baseline = $basePng
                note   = "capture a baseline with -BaselineDir before comparing"
            }
        } else {
            $line = (Invoke-Child "frame_diff.ps1" @{ Files = "$basePng,$capture"; Tolerance = 8 }) -join " "
            $m = [regex]::Match($line, "([0-9.]+)% pixels differ")
            if (-not $m.Success) { throw "could not read frame_diff output: $line" }
            $pct = [double]$m.Groups[1].Value

            # A baseline is only comparable to the build that made it, so the
            # report carries the answer instead of assuming it.
            $sameBuild = $null
            $baseMeta = Join-Path $cd "$State.json"
            if (Test-Path -LiteralPath $baseMeta) {
                try {
                    $bm = Get-Content -LiteralPath $baseMeta -Raw | ConvertFrom-Json
                    $sameBuild = ($bm.binary_sha256 -eq $report.binary_sha256)
                } catch { $sameBuild = $null }
            }

            $report.diff = [ordered]@{
                status         = "compared"
                baseline       = $basePng
                percent        = $pct
                max_percent    = $threshold
                threshold_from = $thresholdSource
                same_build     = $sameBuild
                pass           = ($pct -le $threshold)
                frame_diff     = $line
            }
        }
        $report["compare_dir"] = $cd
    }

    $diffOk = $true
    if ($report.diff["status"] -eq "compared") { $diffOk = [bool]$report.diff["pass"] }
    $report.passed = ($screenOk -and $diffOk)
} finally {
    if ($launched -and -not $KeepRunning) {
        try {
            if (-not $proc.HasExited) {
                $proc.CloseMainWindow() | Out-Null
                if (-not $proc.WaitForExit(20000)) { Stop-Process -Id $proc.Id }
            }
        } catch { }
    }
}

$report | ConvertTo-Json -Depth 6
if ($report.passed) { exit 0 } else { exit 1 }
