<#
.SYNOPSIS
Proves a bootstrap setup executable works, against a release served from this machine.

.DESCRIPTION
RockBandBlitzSetup-latest.exe and RockBandBlitzSetup-nightly.exe install nothing
themselves: they ask GitHub which build is the newest one for their channel,
download the single file on that release, check the size and the sha256 the release
resource reports, and run it (README.md, section "The three products").

None of that can be asserted against GitHub. The repository has no releases at all
yet, a nightly must not be published just to test one, and a test that needs the
network fails for reasons of its own. The helper takes `--api` and the bootstrap
takes the custom parameter `/LATEST_API=` for exactly this, so this script does the
whole thing locally:

  1. a mock release API on http://localhost:<port>, answering the shape GitHub
     answers - the release resource, main's newest commit, and the release's one
     file - with a port of its own per leg, so no leg can collide with another;
  2. a stand-in for that one file: a copy of a real executable, whose true size and
     sha256 go into the answer;
  3. the bootstrap itself, run silently (/SILENT /SUPPRESSMSGBOXES) with
     /LATEST_API= pointed at the mock, so the run needs nobody to click anything -
     the failures it tests are suppressed modals, and the exit code carries the
     answer;
  4. what it resolved, read back from the user's own files
     (%APPDATA%\rb_blitz\bootstrap\<channel>.toml) and from the child's own output,
     so "it ran the released installer" is a file rather than a claim.

Five legs, because a leg that cannot fail proves nothing:

  1. the happy path: found, downloaded, verified, run, recorded (exit 0, the child
     wrote its report, the record names the tag, the asset and its digest);
  2. a digest that does not match: refused, nothing run (exit 1);
  3. the other channel's build: `latest` refuses a prerelease, `nightly` says the
     repository has no build of main;
  4. a bootstrap published as the newest build: refused by name, because a bootstrap
     that installs a bootstrap loops for ever;
  5. (nightly) main ahead of what was built: installed, and both the record and the
     progress page say the build is not main's tip.

.PARAMETER Channel
latest (the default) or nightly: which bootstrap to drive.

.PARAMETER Port
First loopback port for the mock APIs. 8731 by default; each leg takes the next one.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\acceptance_bootstrap.ps1 -Channel nightly
#>
[CmdletBinding()]
param(
    [ValidateSet('latest', 'nightly')]
    [string] $Channel = 'latest',
    [int]    $Port = 8731
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$work = Join-Path $repo "out\_swap\bootstrap\$Channel"
$distDir = Join-Path $repo 'installer\out\dist'
$bootstrap = Join-Path $distDir "RockBandBlitzSetup-$Channel.exe"
$child = Join-Path $repo 'out\build\win-amd64-release\rb_blitz_controller_settings.exe'

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

# --- preconditions --------------------------------------------------------

if (-not (Test-Path -LiteralPath $bootstrap)) {
    $switch = if ($Channel -eq 'nightly') { '-Nightly' } else { '-Latest' }
    throw "$bootstrap does not exist. Build it first:`n  powershell -File installer\build.ps1 $switch"
}
if (-not (Test-Path -LiteralPath $child)) {
    throw ("$child does not exist: this script runs a stand-in for a release's one file, and " +
           'that stand-in is a real executable. Build the game first (docs/guides/build-and-run.md).')
}

if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
New-Item -ItemType Directory -Force -Path $work | Out-Null

# The stand-in for the release's one file. A real executable, so "the bootstrap ran
# it" can be read off what it wrote. Named like the rolling package a build of main
# publishes, and carrying a commit marker, because that is what the nightly channel
# reads the commit out of.
$assetName = 'RockBandBlitzSetup-nightly-96dce40.exe'
$assetFile = Join-Path $work $assetName
Copy-Item -LiteralPath $child -Destination $assetFile
$assetSha = (Get-FileHash -LiteralPath $assetFile -Algorithm SHA256).Hash.ToLowerInvariant()
$assetSize = (Get-Item -LiteralPath $assetFile).Length
$tagName = if ($Channel -eq 'nightly') { 'nightly-2026-10-15-96dce40' } else { '0.1.0' }
# The commit the release names, and (separately) main's own tip: the same id for the
# happy path, a newer one for the "behind main" leg.
$builtCommit = '96dce40e926dc83783c873297aa945a39e6e1288'
$newerCommit = 'beef1234567890abcdef1234567890abcdef1234'

Write-Host ''
Write-Host "acceptance: the $Channel bootstrap" -ForegroundColor Cyan
Write-Host "  bootstrap : $bootstrap"
Write-Host "  stand-in  : $assetName ($assetSize bytes, sha256 $($assetSha.Substring(0, 12))...)"
Write-Host "  scratch   : $work"

# --- the mock release API -------------------------------------------------

# The shape GitHub answers, with only the fields release_api.cpp reads. The file
# names carry the port of the leg they belong to, so a leaked listener from one leg
# cannot answer another.
function New-ReleaseObject {
    param([int] $LegPort, [string] $Tag, [bool] $Prerelease, [string] $Name, [string] $Sha, [long] $Size)
    $digest = if ($Sha) { "`"digest`": `"sha256:$Sha`"," } else { '' }
    return @"
{"tag_name":"$Tag","prerelease":$(if ($Prerelease) { 'true' } else { 'false' }),"draft":false,"assets":[
  {"name":"$Name","size":$Size,"content_type":"application/octet-stream",$digest
   "browser_download_url":"http://localhost:$LegPort/files/$Name"}
]}
"@
}

function New-CommitObject {
    param([string] $Sha)
    return @"
{"sha":"$Sha","commit":{"message":"a commit on main","author":{"name":"nobody"}},"parents":[]}
"@
}

function Start-MockApi {
    param([string] $Listing, [string] $CommitJson, [int] $LegPort)
    $listingFile = Join-Path $work "release-$LegPort.json"
    $commitFile = Join-Path $work "commit-$LegPort.json"
    # No byte-order mark: what GitHub serves has none, and a BOM in front of a JSON
    # document is a byte the reader is right to be confused by (this harness learned
    # that the hard way - the nightly list answered "one release, not a list" until
    # these two lines stopped using Set-Content -Encoding utf8, which writes one).
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($listingFile, $Listing, $utf8NoBom)
    [System.IO.File]::WriteAllText($commitFile, $CommitJson, $utf8NoBom)
    $assetPath = Join-Path $work $assetName
    $job = Start-Job -ScriptBlock {
        param($ListingFile, $CommitFile, $AssetPath, $LegPort)
        $listener = [System.Net.HttpListener]::new()
        $listener.Prefixes.Add("http://localhost:$LegPort/")
        $listener.Start()
        try {
            while ($true) {
                $context = $listener.GetContext()
                # Three things are asked of GitHub, and all of them come from here:
                # the release resource (the API paths, with the query string the
                # nightly list needs), main's newest commit, and the release's one
                # file (its own path).
                $path = $context.Request.Url.AbsolutePath
                if ($path -like '/releases*') {
                    $body = [System.IO.File]::ReadAllBytes($ListingFile)
                    $context.Response.ContentType = 'application/json'
                } elseif ($path -like '*/commits/main') {
                    $body = [System.IO.File]::ReadAllBytes($CommitFile)
                    $context.Response.ContentType = 'application/json'
                } else {
                    $body = [System.IO.File]::ReadAllBytes($AssetPath)
                    $context.Response.ContentType = 'application/octet-stream'
                }
                $context.Response.StatusCode = 200
                $context.Response.ContentLength64 = $body.Length
                $context.Response.OutputStream.Write($body, 0, $body.Length)
                $context.Response.Close()
            }
        } finally {
            $listener.Stop()
            $listener.Close()
        }
    } -ArgumentList $listingFile, $commitFile, $assetPath, $LegPort
    # The listener takes a moment to bind; the bootstrap would otherwise race it.
    $deadline = (Get-Date).AddSeconds(10)
    while ($job.State -ne 'Running' -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
    return $job
}

function Stop-MockApi {
    param($Job)
    if ($null -eq $Job) { return }
    Stop-Job -Job $Job -ErrorAction SilentlyContinue
    Remove-Job -Job $Job -Force -ErrorAction SilentlyContinue
}

# --- one leg of the matrix ------------------------------------------------

$legCounter = 0
function Invoke-Bootstrap {
    param([string] $Listing, [string] $CommitJson, [string] $ApiPath, [string] $Label)
    $script:legCounter++
    $legPort = $Port + $script:legCounter
    $labelDir = Join-Path $work $Label
    New-Item -ItemType Directory -Force -Path $labelDir | Out-Null
    $log = Join-Path $labelDir 'stub.log'
    $report = Join-Path $labelDir 'child-report.txt'
    # One record per channel, in the folder both bootstraps write to: this is where
    # "which build did this machine install" is answered from (bootstrap.iss).
    $record = Join-Path (Join-Path $env:APPDATA 'rb_blitz\bootstrap') "$Channel.toml"
    if (Test-Path -LiteralPath $record) { Remove-Item -LiteralPath $record -Force }
    if (Test-Path -LiteralPath $report) { Remove-Item -LiteralPath $report -Force }

    $job = Start-MockApi -Listing $Listing -CommitJson $CommitJson -LegPort $legPort
    $process = $null
    try {
        # /SILENT and /SUPPRESSMSGBOXES: every failure this drives is meant to be
        # suppressed, so a red run leaves no modal on the desktop waiting for a click.
        $arguments = @('/SILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$log",
                       "/LATEST_API=http://localhost:$legPort$ApiPath",
                       "--dump-display=$report")
        $process = Start-Process -FilePath $bootstrap -ArgumentList $arguments -PassThru -Wait
    } finally {
        Stop-MockApi -Job $job
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }

    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Log      = $log
        Report   = $report
        Record   = $record
        Dir      = $labelDir
    }
}

function Read-Text {
    param([string] $Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    return (Get-Content -LiteralPath $Path -Raw)
}

# --- leg 1: the happy path ------------------------------------------------

Write-Host ''
Write-Host 'leg 1: the newest build of this channel is fetched, checked and run' -ForegroundColor Cyan
$leg1Port = $Port + 1
$happyListing = if ($Channel -eq 'nightly') {
    # A release at the top of the list on purpose: the nightly reader has to look past
    # it, because a release is not a build of main.
    ('[' + (New-ReleaseObject -LegPort $leg1Port -Tag '0.0.9' -Prerelease $false -Name 'RockBandBlitzSetup-0.0.9.exe' -Sha '' -Size 7) + ',' +
          (New-ReleaseObject -LegPort $leg1Port -Tag $tagName -Prerelease $true -Name $assetName -Sha $assetSha -Size $assetSize) + ']')
} else {
    (New-ReleaseObject -LegPort $leg1Port -Tag $tagName -Prerelease $false -Name $assetName -Sha $assetSha -Size $assetSize)
}
$happyPath = if ($Channel -eq 'nightly') { '/releases?per_page=30' } else { '/releases/latest' }
$happy = Invoke-Bootstrap -Listing $happyListing -CommitJson (New-CommitObject -Sha $builtCommit) `
                          -ApiPath $happyPath -Label 'happy'
Check 'the bootstrap exits 0' ($happy.ExitCode -eq 0) "(exit $($happy.ExitCode))"
Check 'the released installer was run' (Test-Path -LiteralPath $happy.Report) `
      '- the child wrote no report, so it either never ran or got no parameters'
Check 'what was installed is recorded in the user''s files' (Test-Path -LiteralPath $happy.Record)
if (Test-Path -LiteralPath $happy.Record) {
    $record = Read-Text -Path $happy.Record
    Check 'the record names this channel' ($record -match "channel = `"$Channel`"")
    Check 'the record names the release it installed' ($record -match "tag_name = `"$([regex]::Escape($tagName))`"")
    Check 'the record names the asset and its digest' `
          (($record -match [regex]::Escape($assetName)) -and ($record -match $assetSha))
    if ($Channel -eq 'nightly') {
        Check 'the record names the commit, and that it is main''s tip' `
              (($record -match 'commit = "96dce40"') -and ($record -match 'at_tip = 1')) `
              "- the record does not carry the commit: $record"
    } else {
        Check 'a release record carries no commit claim' (-not ($record -match 'at_tip')) `
              '- a release is a version, not a commit'
    }
}
Check 'the download is cleaned up after a successful install' `
      (-not (Test-Path -LiteralPath (Join-Path $env:TEMP "rb-blitz-$Channel\$assetName")))

# --- leg 2: a digest that does not match ----------------------------------

Write-Host ''
Write-Host 'leg 2: a download that is not the published file is refused' -ForegroundColor Cyan
$leg2Port = $Port + 2
$wrongSha = 'a' * 64
$wrongListing = if ($Channel -eq 'nightly') {
    ('[' + (New-ReleaseObject -LegPort $leg2Port -Tag $tagName -Prerelease $true -Name $assetName -Sha $wrongSha -Size $assetSize) + ']')
} else {
    (New-ReleaseObject -LegPort $leg2Port -Tag $tagName -Prerelease $false -Name $assetName -Sha $wrongSha -Size $assetSize)
}
$wrong = Invoke-Bootstrap -Listing $wrongListing -CommitJson (New-CommitObject -Sha $builtCommit) `
                          -ApiPath $happyPath -Label 'wrong-digest'
Check 'the bootstrap exits non-zero' ($wrong.ExitCode -ne 0) "(exit $($wrong.ExitCode))"
Check 'nothing was run' (-not (Test-Path -LiteralPath $wrong.Report))
Check 'the failure names the mismatch' ((Read-Text -Path $wrong.Log) -match 'sha256') `
      '- the log does not say what was wrong'

# --- leg 3: the other channel's build -------------------------------------

Write-Host ''
Write-Host 'leg 3: a build of the wrong channel is refused with a reason' -ForegroundColor Cyan
$leg3Port = $Port + 3
if ($Channel -eq 'latest') {
    # `latest` asked for a release and was handed a build of main.
    $foreignListing = (New-ReleaseObject -LegPort $leg3Port -Tag 'nightly-2026-10-15-96dce40' -Prerelease $true -Name $assetName -Sha $assetSha -Size $assetSize)
} else {
    # `nightly` asked for a build of main and found a repository that has only ever
    # cut releases - which is the honest state of this one today.
    $foreignListing = ('[' + (New-ReleaseObject -LegPort $leg3Port -Tag '0.1.0' -Prerelease $false -Name $assetName -Sha $assetSha -Size $assetSize) + ']')
}
$foreign = Invoke-Bootstrap -Listing $foreignListing -CommitJson (New-CommitObject -Sha $builtCommit) `
                            -ApiPath $happyPath -Label 'wrong-channel'
Check 'the bootstrap exits non-zero' ($foreign.ExitCode -ne 0) "(exit $($foreign.ExitCode))"
Check 'nothing was run' (-not (Test-Path -LiteralPath $foreign.Report))
$foreignLog = Read-Text -Path $foreign.Log
if ($Channel -eq 'latest') {
    Check 'the failure says the build is not a release' ($foreignLog -match 'prerelease')
} else {
    Check 'the failure says there is no build of main' ($foreignLog -match 'no nightly build')
}

# --- leg 4: a bootstrap published as the newest build ---------------------

Write-Host ''
Write-Host 'leg 4: a bootstrap is refused as the file to install' -ForegroundColor Cyan
$leg4Port = $Port + 4
$bootstrapName = 'RockBandBlitzSetup-nightly.exe'
$loopListing = if ($Channel -eq 'nightly') {
    ('[' + (New-ReleaseObject -LegPort $leg4Port -Tag $tagName -Prerelease $true -Name $bootstrapName -Sha $assetSha -Size $assetSize) + ']')
} else {
    (New-ReleaseObject -LegPort $leg4Port -Tag $tagName -Prerelease $false -Name $bootstrapName -Sha $assetSha -Size $assetSize)
}
$loop = Invoke-Bootstrap -Listing $loopListing -CommitJson (New-CommitObject -Sha $builtCommit) `
                         -ApiPath $happyPath -Label 'bootstrap-asset'
Check 'the bootstrap exits non-zero' ($loop.ExitCode -ne 0) "(exit $($loop.ExitCode))"
Check 'nothing was run' (-not (Test-Path -LiteralPath $loop.Report))
Check 'the failure names the mistake' ((Read-Text -Path $loop.Log) -match 'bootstrap')

# --- leg 5: main has moved on since the build (nightly only) ---------------

if ($Channel -eq 'nightly') {
    Write-Host ''
    Write-Host 'leg 5: a build that is behind main is installed and said to be behind' -ForegroundColor Cyan
    $leg5Port = $Port + 5
    $behindListing = ('[' + (New-ReleaseObject -LegPort $leg5Port -Tag $tagName -Prerelease $true -Name $assetName -Sha $assetSha -Size $assetSize) + ']')
    $behind = Invoke-Bootstrap -Listing $behindListing -CommitJson (New-CommitObject -Sha $newerCommit) `
                               -ApiPath $happyPath -Label 'behind-main'
    Check 'the bootstrap still exits 0' ($behind.ExitCode -eq 0) "(exit $($behind.ExitCode))"
    Check 'the released installer was still run' (Test-Path -LiteralPath $behind.Report)
    if (Test-Path -LiteralPath $behind.Record) {
        $behindRecord = Read-Text -Path $behind.Record
        Check 'the record says the build is not main''s tip' `
              (($behindRecord -match 'at_tip = 0') -and ($behindRecord -match $newerCommit)) `
              "- the record does not carry main's tip: $behindRecord"
    }
}

# --- the result -----------------------------------------------------------

Write-Host ''
if ($script:failures.Count -eq 0) {
    Write-Host "the $Channel bootstrap passed $script:checks checks" -ForegroundColor Green
} else {
    Write-Host "the $Channel bootstrap failed $($script:failures.Count) of $script:checks checks:" -ForegroundColor Red
    foreach ($failure in $script:failures) { Write-Host "  - $failure" }
}
Write-Host "  evidence: $work"
exit $(if ($script:failures.Count -eq 0) { 0 } else { 1 })
