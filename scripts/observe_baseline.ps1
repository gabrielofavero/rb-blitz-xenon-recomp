# Build the observation baseline set, and measure the same-build noise floor for
# every state in scripts/ui_states.ps1. This is the customization plan's E1
# (docs/plans/customization-plan.md): the reference every later visual claim is
# measured against, and the answer to "how much does this state differ between two
# runs of the same build?" without which no screenshot diff means anything.
#
# It runs scripts/observe_ui.ps1 twice per state:
#
#   pass 1  -BaselineDir   records the reference capture and its metadata
#   pass 2  -Compare       captures again and diffs against pass 1
#
# so the report's `diff.percent` for a state *is* that state's same-build noise
# floor. Two same-build pairs is the minimum that says anything; a state whose two
# passes disagree is a state with no settled frame, and that is a result, not a
# failure - the title screen measured 0.046 % in one pair and 1.978 % in another,
# because what varies is the distance between the runs' phases of its animation.
#
# The baseline lives under <OutDir>/<build>/ where <build> is the first 12 hex
# digits of the executable's SHA-256: a baseline is only comparable to the build
# that made it, and putting the identity in the path makes a mismatch impossible to
# miss rather than something a report has to be read to notice.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\observe_baseline.ps1
#   ... -States title,main-menu          # a subset
#   ... -OnePass                         # captures only; no noise floor
#
# It is a measurement, so it exits 0 even when a state fails its own expectation:
# the failures are in the summary, which is the point of running it.
param(
    [string]$OutDir = "out/observations/baseline",
    [string[]]$States = @(),
    [int]$SettleSeconds = -1,
    [int]$CompareScale = 1,
    [switch]$OnePass,
    # Skip the capture pass for a state whose baseline is already in place. A run
    # of this script is a long sequence of boots, so it gets interrupted; resuming
    # reuses the captures that succeeded instead of repeating them, and the state
    # list it reports is still the whole set.
    [switch]$Resume,
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$GameRoot,
    [string]$UltimateMode = "0"
)

$ErrorActionPreference = "Stop"
# `powershell -File` hands `-States a,b,c` over as one comma-joined string, so
# split it back into separate names - the same trap drive_ui.ps1 documents.
if ($States.Count -eq 1 -and $States[0].Contains(",")) { $States = $States[0] -split "," }
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path (Join-Path $root $BuildDir) "rb_blitz.exe"
if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }

function Invoke-Harness([string[]]$Argv) {
    # The execution policy is Restricted here, so the harness (and everything it
    # drives) goes through a child PowerShell - scripts/observe_ui.ps1's own rule.
    $full = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
              (Join-Path $PSScriptRoot "observe_ui.ps1")) + $Argv
    # A state that fails its own expectation exits 1 and says so on stderr; that
    # is data this script collects, not an error for it to stop on, and with
    # ErrorActionPreference = Stop a native command's stderr would terminate the
    # loop instead.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { $out = & powershell @full 2>&1 } finally { $ErrorActionPreference = $previous }
    return @{ Exit = $LASTEXITCODE; Text = $out }
}

# The harness prints exactly one JSON object on stdout, last; anything a child
# script echoed before it is discarded by taking from the first brace.
function Convert-Report([string[]]$Text) {
    $joined = ($Text | ForEach-Object { "$_" }) -join "`n"
    $i = $joined.IndexOf("{")
    if ($i -lt 0) { return $null }
    try { return ($joined.Substring($i) | ConvertFrom-Json) } catch { return $null }
}

$states = if ($States.Count -gt 0) { $States } else {
    (Invoke-Harness @("-ListStates")).Text | ConvertFrom-Json | ForEach-Object { $_.state }
}

$build = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.Substring(0, 12).ToLowerInvariant()
$baseDir = Join-Path (Join-Path $root $OutDir) $build
New-Item -ItemType Directory -Force -Path $baseDir | Out-Null

Write-Host "baseline $build -> $baseDir"
Write-Host ("states: {0}" -f ($states -join ", "))

$rows = @()
foreach ($state in $states) {
    $common = @("-State", $state, "-UltimateMode", $UltimateMode, "-SettleSeconds", "$SettleSeconds")
    $basePng = Join-Path $baseDir "$state.png"
    $baseMeta = Join-Path $baseDir "$state.json"

    $first = $null
    $firstReport = $null
    $resumed = $false
    $baselineScale = $CompareScale
    if ($Resume -and (Test-Path -LiteralPath $basePng) -and (Test-Path -LiteralPath $baseMeta)) {
        # The capture is already there; read its metadata rather than boot again.
        # The metadata carries the capture's size, the OCR verdict and the scale its
        # floor belongs to, so the summary says what the first pass would have said.
        $resumed = $true
        $meta = Get-Content -LiteralPath $baseMeta -Raw | ConvertFrom-Json
        if ($null -ne $meta.compare_scale) { $baselineScale = [int]$meta.compare_scale }
        # A baseline written before the verdict was recorded still has the needle
        # and the text it was matched against, so the same test can be repeated
        # rather than the state being reported as unverified.
        $matched = $meta.needle_matched
        if ($null -eq $matched -and [string]$meta.needle -ne "") {
            $matched = ([string]$meta.ocr_text).ToUpperInvariant().Contains(([string]$meta.needle).ToUpperInvariant())
        }
        $size = ""
        try {
            Add-Type -AssemblyName System.Drawing
            $img = [System.Drawing.Image]::FromFile($basePng)
            $size = "$($img.Width)x$($img.Height)"
            $img.Dispose()
        } catch { }
        $ref = [ordered]@{ capture = $basePng; capture_size = $size; ocr = [ordered]@{
            needle = $meta.needle; matched = $matched; text = $meta.ocr_text } ; passed = $true }
        $firstReport = $ref
    } else {
        $first = Invoke-Harness ($common + @("-Ocr", "-BaselineDir", $baseDir))
        $firstReport = Convert-Report $first.Text
    }

    $second = $null
    $secondReport = $null
    if (-not $OnePass) {
        $second = Invoke-Harness ($common + @("-Compare", $baseDir, "-CompareScale", "$CompareScale"))
        $secondReport = Convert-Report $second.Text
    }

    $diffPercent = $null
    $threshold = $null
    $thresholdFrom = $null
    $sameBuild = $null
    $diffStatus = "not-run"
    if ($secondReport) {
        $diffStatus = $secondReport.diff.status
        if ($diffStatus -eq "compared") {
            $diffPercent = $secondReport.diff.percent
            $threshold = $secondReport.diff.max_percent
            $thresholdFrom = $secondReport.diff.threshold_from
            $sameBuild = $secondReport.diff.same_build
        }
    }

    $row = [ordered]@{
        state          = $state
        capture        = if ($firstReport) { $firstReport.capture } else { "" }
        capture_size   = if ($firstReport) { $firstReport.capture_size } else { "" }
        needle         = if ($firstReport) { $firstReport.ocr.needle } else { "" }
        needle_matched = if ($firstReport) { $firstReport.ocr.matched } else { $null }
        ocr_text       = if ($firstReport) { $firstReport.ocr.text } else { "" }
        resumed        = $resumed
        first_pass_ok  = if ($firstReport) { $firstReport.passed } else { $false }
        first_exit     = if ($resumed) { 0 } elseif ($first) { $first.Exit } else { $null }
        noise_percent  = $diffPercent
        noise_scale    = $CompareScale
        baseline_scale = $baselineScale
        threshold      = $threshold
        threshold_from = $thresholdFrom
        same_build     = $sameBuild
        diff_status    = $diffStatus
        second_exit    = if ($second) { $second.Exit } else { $null }
    }
    $rows += $row

    $floor = if ($null -ne $diffPercent) { "{0}%" -f $diffPercent } else { $diffStatus }
    Write-Host ("  {0,-14} ocr={1,-5} floor={2,-10} capture={3}" -f `
        $state, "$($row.needle_matched)", $floor, $row.capture_size)
    if ($diffStatus -eq "compared" -and $baselineScale -ne $CompareScale) {
        Write-Host ("    ! baseline captured at scale {0}, compared at {1} - its floor does not transfer" -f `
            $baselineScale, $CompareScale)
    }
}

$summary = [ordered]@{
    build          = $build
    binary_sha256  = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    captured_at    = (Get-Date).ToString("o")
    ultimate_mode  = $UltimateMode
    settle_seconds = $SettleSeconds
    compare_scale  = $CompareScale
    one_pass       = [bool]$OnePass
    baseline_dir   = $baseDir
    states         = $rows
}
$summaryPath = Join-Path $baseDir "summary.json"
Set-Content -LiteralPath $summaryPath -Value ($summary | ConvertTo-Json -Depth 6) -Encoding UTF8

Write-Host ""
Write-Host "summary: $summaryPath"
# Counted explicitly rather than with Where-Object/Measure-Object: the rows are
# ordered hashtables, whose keys are not properties to those cmdlets.
$withNeedle = 0
$matchedCount = 0
$floorCount = 0
$floorMin = $null
$floorMax = $null
foreach ($r in $rows) {
    if ([string]$r.needle -ne "") {
        $withNeedle++
        if ($r.needle_matched -eq $true) { $matchedCount++ }
    }
    if ($null -ne $r.noise_percent) {
        $floorCount++
        $p = [double]$r.noise_percent
        if ($null -eq $floorMin -or $p -lt $floorMin) { $floorMin = $p }
        if ($null -eq $floorMax -or $p -gt $floorMax) { $floorMax = $p }
    }
}
Write-Host ("states measured: {0}; needs pinned: {1}, matched: {2}" -f $rows.Count, $withNeedle, $matchedCount)
if ($floorCount -gt 0) {
    Write-Host ("same-build noise floor at scale {0} over {1} state(s): {2}% .. {3}%" -f `
        $CompareScale, $floorCount, $floorMin, $floorMax)
}
exit 0
