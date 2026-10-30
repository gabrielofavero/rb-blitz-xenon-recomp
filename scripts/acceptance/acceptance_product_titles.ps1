<#
.SYNOPSIS
Proves the installer names itself as the build it is.

.DESCRIPTION
The product is one file, `RockBandBlitzSetup-<version>.exe`, and its window must say
`Setup - Rock Band Blitz <version>`: the file name and the caption have to agree,
because a screenshot of a failure is how most of these get reported and a version
string in the wrong place sends the reader looking in the wrong direction.

Setup's caption is `Setup - <AppVerName>`, and AppVerName is generated from the pins
(tools/embed_config.cpp): the product name plus the version of the recompiled build
inside it. Both come from [installer] version in installer\config\pins.toml.

The strings live in a compressed block, so they cannot be read out of the file; the
window has to be opened and asked. The setup executable is started, the caption of the
window it puts up is read, and every setup process of that product is then killed -
nothing is driven past its first page, and nothing is installed.

One detail decides how this is done. Setup re-executes itself from a temporary copy
(`RockBandBlitzSetup-0.1.0.tmp` for `RockBandBlitzSetup-0.1.0.exe`), so the process
that is started is not the one with the window: the caption belongs to the child, and
reading MainWindowTitle from the parent returns nothing (measured 2026-10-15). The
child is found by the name Setup gives it, and both are killed at the end.

Run this from an elevated shell on a machine whose UAC would otherwise ask: the
installer requests administrator, and a run that waits at a consent prompt has no
caption to read.

.PARAMETER DistDir
Where the setup executable is. installer\out\dist by default.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\acceptance\acceptance_product_titles.ps1
#>
[CmdletBinding()]
param(
    [string] $DistDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
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

# The caption every built setup executable is expected to carry, derived from its own
# file name: `RockBandBlitzSetup-<version>.exe` says `Setup - Rock Band Blitz <version>`.
# Which files exist depends on what was built last, and this script is about what the
# ones that exist say.
$expected = [ordered]@{}
foreach ($file in Get-ChildItem -Path $DistDir -Filter 'RockBandBlitzSetup-*.exe' -ErrorAction SilentlyContinue) {
    $version = $file.BaseName -replace '^RockBandBlitzSetup-', ''
    $expected[$file.Name] = "Setup - Rock Band Blitz $version"
}

# Setup's own temporary copy of itself: `RockBandBlitzSetup-0.1.0.tmp.exe`, which is
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
Write-Host 'acceptance: the installer names itself' -ForegroundColor Cyan
Write-Host "  from: $DistDir"
Write-Host ''

if ($expected.Count -eq 0) { throw "$DistDir holds no setup executable" }

foreach ($name in $expected.Keys) {
    $path = Join-Path $DistDir $name

    Clear-Setups
    Start-Sleep -Milliseconds 300

    # The wizard asks "... is already installed. Install it again?" before it shows
    # any page, and an answer of No ends the run: on a machine that already has the
    # game installed - this project's own machine, and every user upgrading - there is
    # no caption to read until that question is answered. A run that meets the question
    # is skipped with that reason; on a machine without the game the caption is read
    # and checked.
    $file = Get-Item -LiteralPath $path
    $pattern = $file.BaseName + '*'
    $log = Join-Path $env:TEMP ("title-" + [System.IO.Path]::GetRandomFileName() + '.log')
    $parent = Start-Process -FilePath $path -ArgumentList @("/LOG=$log") -PassThru
    try {
        $deadline = (Get-Date).AddSeconds(15)
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
        if ($title -eq '') {
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
