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
# Three rules it enforces rather than documents:
#
#   * **A pixel diff is only evidence against a same-build baseline.** Without
#     -BaselineDir there is nothing to compare, and -Compare with no baseline
#     file for the state reports `no-baseline` and does NOT fail the run - a
#     measurement with nothing to measure against is not a pass (docs/backlog.md,
#     "Measure the noise floor before believing any screenshot diff").
#   * **The comparison is against the state's settled frame.** The route's own
#     waits are not enough: a screen is only comparable once it has stopped
#     animating, which is why every state carries a settle time.
#   * **A state whose frame never settles does not gate on the diff.** Measured
#     on this build: two title-screen runs that both OCR'd as 'TO START' differed
#     by 18.28 % and 10.74 % of the frame, so a ceiling taken from one pair is not
#     a floor there - what moves is an animation, and no settle time removes a
#     phase difference. Such a state carries `Settled = $false` (ui_states.ps1):
#     its diff is reported as corroboration and the OCR needle is the control.
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
    # names its own ceiling (ui_states.ps1). It gates the run only for a state
    # with a settled frame; a state marked `Settled = $false` is one whose frame
    # never settles, where the diff is corroboration and the OCR needle is the
    # control.
    [double]$MaxDiffPercent = 1.0,
    # Downscale both frames by this factor before diffing them. The measured
    # difference is scale-dependent - a fix that moves a few pixels is a smaller
    # percentage of a bigger frame, and the backlog's 3.5-5.9 % was measured on
    # ~1 Mpx captures while a capture here is 8.3 Mpx - so this is recorded in the
    # report and in the baseline metadata, and a floor is never compared across
    # scales. 1 diffs the frames as captured.
    [int]$CompareScale = 1,
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

# A downscaled copy, for -CompareScale: frame_diff walks every pixel in
# PowerShell, so the cost of a comparison is the frame's area, and a smaller frame
# is also closer to the scale the project's older noise-floor numbers came from.
function New-ScaledCopy([string]$Path, [int]$Scale, [string]$OutPath) {
    Add-Type -AssemblyName System.Drawing
    $src = [System.Drawing.Image]::FromFile($Path)
    try {
        $w = [Math]::Max(1, [int]($src.Width / $Scale))
        $h = [Math]::Max(1, [int]($src.Height / $Scale))
        $bmp = New-Object System.Drawing.Bitmap $w, $h
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.DrawImage($src, 0, 0, $w, $h)
        $g.Dispose()
        $bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
    } finally { $src.Dispose() }
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
            settled        = if ($s.ContainsKey("Settled")) { [bool]$s.Settled } else { $true }
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
# A state is settled unless it says otherwise (ui_states.ps1). Measured: the two
# title-screen runs that OCR'd identically as 'TO START' differed by 18.28 % and
# 10.74 % of the frame, so no ceiling taken from one pair gates those screens -
# their frame is an animation, and the diff cannot tell a phase from a change.
$settled = if ($spec.ContainsKey("Settled")) { [bool]$spec.Settled } else { $true }

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
            crop           = [string]$spec.Crop
            ocr_text       = $report.ocr.text
            # The verdict, not just the text: -Resume reads this file instead of
            # booting again, so the summary has to be able to repeat what the
            # capture pass found without re-OCRing the capture.
            needle_matched = $report.ocr.matched
            compare_scale  = $CompareScale
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
            $diffA = $basePng
            $diffB = $capture
            $scaleDir = $null
            if ($CompareScale -gt 1) {
                $scaleDir = Join-Path ([System.IO.Path]::GetTempPath()) ("rbblitz-scale-" + [guid]::NewGuid().ToString("N"))
                New-Item -ItemType Directory -Force -Path $scaleDir | Out-Null
                $diffA = Join-Path $scaleDir "baseline.png"
                $diffB = Join-Path $scaleDir "capture.png"
                New-ScaledCopy $basePng $CompareScale $diffA
                New-ScaledCopy $capture $CompareScale $diffB
            }
            try {
                $line = (Invoke-Child "frame_diff.ps1" @{ Files = "$diffA,$diffB"; Tolerance = 8 }) -join " "
            } finally {
                if ($scaleDir) { Remove-Item -Recurse -Force $scaleDir -ErrorAction SilentlyContinue }
            }
            $m = [regex]::Match($line, "([0-9.]+)% pixels differ")
            if (-not $m.Success) { throw "could not read frame_diff output: $line" }
            $pct = [double]$m.Groups[1].Value

            # A baseline is only comparable to the build that made it, and its
            # floor is only a floor at the scale it was measured at, so the report
            # carries both answers instead of assuming them.
            $sameBuild = $null
            $baseScale = $null
            $baseMeta = Join-Path $cd "$State.json"
            if (Test-Path -LiteralPath $baseMeta) {
                try {
                    $bm = Get-Content -LiteralPath $baseMeta -Raw | ConvertFrom-Json
                    $sameBuild = ($bm.binary_sha256 -eq $report.binary_sha256)
                    if ($null -ne $bm.compare_scale) { $baseScale = [int]$bm.compare_scale }
                } catch { $sameBuild = $null }
            }
            # Unknown (a baseline written before the scale was recorded) is not a
            # mismatch; it is only something this report cannot claim either way.
            $sameScale = if ($null -eq $baseScale) { $null } else { $baseScale -eq $CompareScale }

            $gates = ($settled -and ($sameScale -ne $false))
            $report.diff = [ordered]@{
                status         = "compared"
                baseline       = $basePng
                percent        = $pct
                scale          = $CompareScale
                baseline_scale = $baseScale
                same_scale     = $sameScale
                max_percent    = $threshold
                threshold_from = $thresholdSource
                same_build     = $sameBuild
                settled        = $settled
                gates_pass     = $gates
                pass           = ($pct -le $threshold)
                frame_diff     = $line
            }
        }
        $report["compare_dir"] = $cd
    }

    # The diff fails the run only where it can be believed: a settled state
    # measured against a same-scale baseline. Everywhere else it is reported and
    # not acted on.
    $diffOk = $true
    if ($report.diff["status"] -eq "compared" -and $report.diff["gates_pass"]) {
        $diffOk = [bool]$report.diff["pass"]
    }
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
