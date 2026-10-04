# Ultimate acceptance: the same executable, booted against a game root with an
# installed Rock Band Blitz Ultimate payload, reaches the menu and plays a named
# song -- and the vanilla route still passes unchanged.
#
# This is acceptance criterion 1 of docs/ultimate-compat.md section 10 with
# evidence instead of an observation: the route itself is driven by
# scripts/acceptance_song.ps1 (the proven launch-to-results driver), and this
# script adds the two halves the payload needs:
#
#   * a payload leg with the mode left at its default (--ultimate_mode=1, auto),
#     which has to show the mount in the log -- the payload line, the content
#     device string patch, the overlay device and the d:/game: re-point -- and
#     has to show the payload's content actually executing (the guest asks for
#     Ultimate's own game:\ulti_settings.dta, a name that appears in neither
#     decrypted image and so can only come out of the payload's ark);
#   * a vanilla control leg (--ultimate_mode=0) on the same root, which has to
#     pass the same route while showing none of those markers.
#
# Both legs also refuse the dirty-disc abort: with the disk-error latch patch
# off, the payload's ark is not in the retail checksum database and the boot
# ends on XamShowDirtyDiscErrorUI with a black screen (docs/ultimate-compat.md
# section 7), so its presence in a leg's log is a failure even if the route
# somehow continued.
#
# Usage:
#   .\scripts\acceptance_ultimate.ps1                 # payload + vanilla, "These Days"
#   .\scripts\acceptance_ultimate.ps1 -SkipVanilla    # payload leg only
#
# Evidence lands in out/m5-ultimate/{ultimate,vanilla}/ (screenshots, per-run
# logs and summary.json from the song acceptance) plus a combined summary.json.
# Exit code 0 means both legs passed and every assertion held.
param(
    [int]$Runs = 1,
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m5-ultimate",
    [string]$GameRoot,
    [int]$Song = 3,
    [string]$SongName = "THESE DAYS",
    # The mod's payload directory. Empty means <GameRoot>\ultimate, which is
    # where docs/ultimate-compat.md section 8 installs it.
    [string]$PayloadRoot,
    [switch]$SkipVanilla
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work "rb_blitz.exe"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
if (-not $PayloadRoot) { $PayloadRoot = Join-Path $GameRoot "ultimate" }
$capDir = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }
# A payload is present when the game can find the patch header; the pair has to
# be complete or the game reads it as a damaged disc (docs/ultimate-compat.md
# section 2).
$header = Join-Path $PayloadRoot "gen/patch_xbox.hdr"
$archive = Join-Path $PayloadRoot "gen/patch_xbox_0.ark"
if (-not (Test-Path $header)) { throw "no Ultimate payload at $PayloadRoot (missing gen/patch_xbox.hdr)" }
if (-not (Test-Path $archive)) { throw "incomplete Ultimate payload at $PayloadRoot (gen/patch_xbox.hdr without gen/patch_xbox_0.ark)" }

# ------------------------------------------------------------- the two legs ---

# Every helper that touches a .ps1 file has to go through a child PowerShell:
# this machine's execution policy is Restricted (docs/build-and-run.md section 0).
function Invoke-SongLeg([string]$Leg, [string]$Mode) {
    $legDir = Join-Path $OutDir $Leg
    $argv = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "acceptance_song.ps1"),
        "-Runs", [string]$Runs,
        "-BuildDir", $BuildDir,
        "-OutDir", $legDir,
        "-GameRoot", $GameRoot,
        "-Song", [string]$Song,
        "-SongName", $SongName,
        "-UltimateMode", $Mode
    )
    Write-Host "=== $Leg leg (--ultimate_mode=$Mode) ==="
    $out = & powershell @argv 2>&1
    $exit = $LASTEXITCODE
    $out | Out-String | Write-Host
    return [pscustomobject]@{ Leg = $Leg; Mode = $Mode; Dir = (Join-Path $root $legDir); Exit = $exit }
}

# The song acceptance reports each run in summary.json; with -Runs 1 PowerShell's
# ConvertTo-Json writes the run as an object rather than a one-element array, so
# wrap it before indexing.
function Get-SongRuns([string]$Dir) {
    $file = Join-Path $Dir "summary.json"
    if (-not (Test-Path $file)) { return @() }
    return @(Get-Content -LiteralPath $file -Raw | ConvertFrom-Json)
}

function Get-RunLogText([string]$Dir) {
    $text = ""
    foreach ($f in Get-ChildItem (Join-Path $Dir "run*.log") -ErrorAction SilentlyContinue) {
        $text += (Get-Content -LiteralPath $f.FullName -Raw)
    }
    return $text
}

# What each leg's log has to prove. Contains() rather than -match, so the
# device paths' backslashes stay literal.
function Test-LegLog([string]$Leg, [string]$Text) {
    $checks = New-Object System.Collections.ArrayList
    $wanted = @(
        @("payload recognised, mask 0x7", ($Text.Contains("ultimate: payload ") -and $Text.Contains("patches 0x7"))),
        @("content device patched to D:", $Text.Contains("ultimate: content device string at 0x8205DD74 patched: UPDATE: -> D:")),
        @("overlay device mounted", $Text.Contains("ultimate: overlay \Device\BlitzOverlay =")),
        @("d: and game: re-pointed at the overlay", $Text.Contains("Registered symbolic link: d: => \Device\BlitzOverlay")),
        @("payload content live (ulti_settings lookup)", $Text.Contains("path='game:\ulti_settings.dta'")),
        @("no dirty-disc abort", (-not $Text.Contains("XamShowDirtyDiscErrorUI"))),
        @("game data not MODIFIED", (-not $Text.Contains("MODIFIED")))
    )
    if ($Leg -eq "vanilla") {
        # Same rows, inverted: the vanilla route has to show the retail decision
        # and none of the payload markers.
        $wanted = @(
            @("mode reports the retail route", $Text.Contains("ultimate: off, booting the retail game data")),
            @("no payload line", (-not $Text.Contains("ultimate: payload "))),
            @("no overlay device", (-not $Text.Contains("ultimate: overlay \Device\BlitzOverlay ="))),
            @("no payload content", (-not $Text.Contains("path='game:\ulti_settings.dta'"))),
            @("no dirty-disc abort", (-not $Text.Contains("XamShowDirtyDiscErrorUI"))),
            @("game data not MODIFIED", (-not $Text.Contains("MODIFIED")))
        )
    }
    foreach ($w in $wanted) {
        $checks.Add([pscustomobject]@{ Leg = $Leg; Check = $w[0]; Ok = [bool]$w[1] }) | Out-Null
    }
    return $checks.ToArray()
}

# ------------------------------------------------------------------- main ---

$legs = @(Invoke-SongLeg "ultimate" "1")
if (-not $SkipVanilla) { $legs += Invoke-SongLeg "vanilla" "0" }

$rows = New-Object System.Collections.ArrayList
$checks = New-Object System.Collections.ArrayList
foreach ($leg in $legs) {
    $runs = @(Get-SongRuns $leg.Dir)
    $text = Get-RunLogText $leg.Dir
    $legChecks = @(Test-LegLog $leg.Leg $text)
    $checks += $legChecks
    $okRoute = @($runs | Where-Object {
        $_.Boot -and $_.SongList -and $_.Playing -and $_.Results -and $_.Clean -and -not $_.Fatal
    }).Count
    $okChecks = @($legChecks | Where-Object { $_.Ok }).Count
    $rows += [pscustomobject]@{
        Leg = $leg.Leg; Mode = $leg.Mode; Exe = $leg.Exit
        Runs = @($runs).Count; RoutePass = $okRoute
        LogChecks = $okChecks; ChecksTotal = $legChecks.Count
    }
}

Write-Host "`n=== summary ==="
$rows | Format-Table -AutoSize | Out-String | Write-Host
$checks | Format-Table -AutoSize | Out-String | Write-Host

$bad = @($rows | Where-Object { $_.Exe -ne 0 -or $_.RoutePass -ne $_.Runs -or $_.LogChecks -ne $_.ChecksTotal })
$failed = @($checks | Where-Object { -not $_.Ok } | ForEach-Object { "$($_.Leg): $($_.Check)" })
if ($failed) { Write-Host ("failed evidence: " + ($failed -join "; ")) }

[pscustomobject]@{ Runs = $Runs; Song = $Song; SongName = $SongName; Legs = $rows; Checks = $checks } |
    ConvertTo-Json -Depth 5 | Set-Content (Join-Path $capDir "summary.json")

# A pass is: every leg's route reached the results screen naming the song with no
# [FATAL] and a clean exit, and every leg's log carried the evidence its variant
# is supposed to show.
if ($bad.Count -ne 0) { exit 1 }
exit 0
