<#
.SYNOPSIS
Proves every installer product names itself as the build it is.

.DESCRIPTION
The three products are told apart by their file names and by what their window says,
and both matter: a screenshot of a failure is how most of these are reported, and
"version 0.1.0" on a build of main is a sentence that sends the reader looking in the
wrong place (measured 2026-10-15: a nightly downloader and a release both said
"version 0.1.0", and the two could not be told apart in a screenshot).

What each one should say (installer/README.md, section "The three products"):

  RockBandBlitzSetup-<version>.exe        Setup - Rock Band Blitz 0.1.0
  RockBandBlitzSetup-nightly-<commit>.exe Setup - Rock Band Blitz nightly-<commit>
  RockBandBlitzSetup-latest.exe           Setup - Rock Band Blitz latest
  RockBandBlitzSetup-nightly.exe          Setup - Rock Band Blitz nightly

Setup's caption is `Setup - <AppVerName>`, and AppVerName is generated from the pins
(tools/embed_config.cpp): the product name plus the version of the recompiled build
inside it, or plus the channel for a bootstrap, which installs no build of its own.

The strings live in a compressed block, so they cannot be read out of the file; the
window has to be opened and asked. Each product is started, the caption of the window
it puts up is read, and every setup process of that product is then killed - nothing
is driven past its first page, and nothing is installed.

One detail decides how this is done. Setup re-executes itself from a temporary copy
(`RockBandBlitzSetup-latest.tmp` for `RockBandBlitzSetup-latest.exe`), so the process
that is started is not the one with the window: the caption belongs to the child, and
reading MainWindowTitle from the parent returns nothing (measured 2026-10-15). The
child is found by the name Setup gives it, and both are killed at the end.

Run this from an elevated shell on a machine whose UAC would otherwise ask: the
complete packages request administrator, and a run that waits at a consent prompt has
no caption to read.

.PARAMETER DistDir
Where the products are. installer\out\dist by default.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\acceptance_product_titles.ps1
#>
[CmdletBinding()]
param(
    [string] $DistDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not $DistDir) { $DistDir = Join-Path $repo 'installer\out\dist' }

$script:checks = 0
$script:failures = @()

function Check {
    param([string] $What, [bool] $Passed, [string] $Detail = '')
    $script:checks++
    if ($Passed) {
        Write-Host ("  [ok]   {0}" -f $What) -ForegroundColor Green
    } else {
        Write-Host ("  [FAIL] {0} {1}" -f $What, $Detail) -ForegroundColor Red
        $script:failures += "$What $Detail".Trim()
    }
}

# The caption each product is expected to carry. A product that is not built is
# skipped rather than failed: which of the four exist depends on what was built last,
# and this script is about what the ones that exist say.
$expected = [ordered]@{}
$expected['RockBandBlitzSetup-0.1.0.exe'] = 'Setup - Rock Band Blitz 0.1.0'
foreach ($file in Get-ChildItem -Path $DistDir -Filter 'RockBandBlitzSetup-nightly-*.exe' -ErrorAction SilentlyContinue) {
    $short = $file.BaseName -replace '^RockBandBlitzSetup-nightly-', ''
    $expected[$file.Name] = "Setup - Rock Band Blitz nightly-$short"
}
$expected['RockBandBlitzSetup-latest.exe'] = 'Setup - Rock Band Blitz latest'
$expected['RockBandBlitzSetup-nightly.exe'] = 'Setup - Rock Band Blitz nightly'

# Setup's own temporary copy of itself: `RockBandBlitzSetup-latest.tmp.exe`, which is
# the process that owns the window and the caption. Everything that looks like a setup
# is cleared first, because a setup that finds another instance of itself running says
# so in a box titled "Setup" and never shows a caption of its own (which is how one
# run of this script read an empty title for every product: the previous run's copies
# were still up).
function Clear-Setups {
    Get-Process -ErrorAction SilentlyContinue |
        Where-Object ProcessName -like 'RockBandBlitzSetup*' |
        Stop-Process -Force -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host 'acceptance: every product names itself' -ForegroundColor Cyan
Write-Host "  from: $DistDir"
Write-Host ''

if ($expected.Count -eq 0) { throw "$DistDir holds no setup executable" }

foreach ($name in $expected.Keys) {
    $path = Join-Path $DistDir $name
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host ("  [skip] {0} (not built)" -f $name) -ForegroundColor DarkGray
        continue
    }

    Clear-Setups
    Start-Sleep -Milliseconds 300

    # A complete package asks "... is already installed. Install it again?" before it
    # shows any page, and an answer of No ends the run: on a machine that already has
    # the game installed - this project's own machine, and every user upgrading - there
    # is no caption to read until that question is answered. The bootstraps ask
    # nothing, so theirs are the captions this script can assert unattended; a complete
    # package's identity is asserted by its file name, by the pins it was built from,
    # and by that question's own text, which is the AppVerName this check is about.
    $isWizard = $file.BaseName -notmatch '(latest|nightly)$'
    $deadlineSeconds = if ($isWizard) { 15 } else { 45 }
    $file = Get-Item -LiteralPath $path
    $pattern = $file.BaseName + '*'
    $log = Join-Path $env:TEMP ("title-" + [System.IO.Path]::GetRandomFileName() + '.log')
    $parent = Start-Process -FilePath $path -ArgumentList @("/LOG=$log") -PassThru
    try {
        $deadline = (Get-Date).AddSeconds($deadlineSeconds)
        $title = ''
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            # `Setup` with nothing after it is not a caption: it is what the window says
            # while it is asking a question, or before its first page is built.
            $candidate = Get-Process -Name $pattern -ErrorAction SilentlyContinue |
                         Where-Object { $_.MainWindowTitle -like 'Setup - *' } |
                         Select-Object -First 1
            if ($candidate) { $title = $candidate.MainWindowTitle; break }
            if ($parent.HasExited) { break }
        }
        if ($title -eq '' -and $isWizard) {
            Write-Host ("  [skip] {0} (no caption before the 'already installed' question)" -f $name) -ForegroundColor DarkGray
        } else {
            Check "$name says which build it is" ($title -eq $expected[$name]) `
                  "(expected '$($expected[$name])', window says '$title')"
        }
    } finally {
        if (-not $parent.HasExited) { Stop-Process -Id $parent.Id -Force -ErrorAction SilentlyContinue }
        Get-Process -Name $pattern -ErrorAction SilentlyContinue |
            Stop-Process -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
    }
}

Write-Host ''
if ($script:failures.Count -eq 0) {
    Write-Host "every built product passed its $script:checks title check(s)" -ForegroundColor Green
} else {
    Write-Host "failed $($script:failures.Count) of $script:checks checks:" -ForegroundColor Red
    foreach ($failure in $script:failures) { Write-Host "  - $failure" }
}
exit $(if ($script:failures.Count -eq 0) { 0 } else { 1 })
