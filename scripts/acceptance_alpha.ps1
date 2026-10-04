# Alpha acceptance: the two features an alpha cannot ship without, run against
# both build kinds.
#
#   * Saving - scripts/acceptance_save.ps1 changes the manual-calibration offset
#     through the guest's own menus, quits, restarts and requires the value to
#     still be there, then reverts it.
#   * DLC loading - scripts/acceptance_dlc.ps1 requires the DLC root's packages to
#     be enumerated by the guest and a DLC-only song to appear in the song list,
#     with a no-DLC control leg that has to *not* show it.
#
# Two build kinds are run, because they are not the same artefact:
#
#   * dev       - out/build/win-amd64-release/rb_blitz.exe, the tree the build
#                 produces, which also carries the (gitignored) rb_blitz.toml.
#   * installed - the payload the installer ships: rb_blitz.exe and its runtime
#                 DLLs snapshotted by installer/tools/make_payload.ps1 into a bare
#                 install folder with no rb_blitz.toml, launched with that folder
#                 as the working directory exactly as the installer's shortcut
#                 does (setup.iss: --game_data_root="{app}\game", WorkingDir="{app}").
#                 The game data is the repository's `game` tree rather than a copy
#                 of it: the installer only copies that tree in place, and copying
#                 ~1.2 GB would not change which binaries run. See -SkipInstalled.
#
# Usage:
#   .\scripts\acceptance_alpha.ps1
#   .\scripts\acceptance_alpha.ps1 -SkipInstalled        # dev only
#   .\scripts\acceptance_alpha.ps1 -Steps 3              # -15 ms calibration
#
# Writes out/m7-dlc, out/m7-save and out/m7-installed (payload + its two suites),
# prints a table and exits non-zero unless every suite passed.
param(
    [string]$DevBuildDir = "out/build/win-amd64-release",
    [string]$GameRoot,
    [string]$InstalledDir = "out/m7-installed",
    [string]$ExpectedSong = "KIDS IN THE STREET",
    [int]$Steps = 2,
    [switch]$SkipInstalled,
    [switch]$SkipPayloadRefresh
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }

function Invoke-Suite([string]$Label, [string]$BuildDir, [string]$SuiteOut) {
    Write-Host "`n########## $Label ##########"
    $dlcOut = "$SuiteOut/dlc"
    $saveOut = "$SuiteOut/save"
    $results = [ordered]@{ Label = $Label; BuildDir = $BuildDir; Dlc = 1; Save = 1 }

    $dlcArgv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "acceptance_dlc.ps1"),
                 "-BuildDir", $BuildDir, "-OutDir", $dlcOut, "-GameRoot", $GameRoot, "-ExpectedSong", $ExpectedSong)
    & powershell @dlcArgv 2>&1 | ForEach-Object { Write-Host $_ }
    $results.Dlc = $LASTEXITCODE

    $saveArgv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "acceptance_save.ps1"),
                  "-BuildDir", $BuildDir, "-OutDir", $saveOut, "-GameRoot", $GameRoot, "-Steps", [string]$Steps)
    & powershell @saveArgv 2>&1 | ForEach-Object { Write-Host $_ }
    $results.Save = $LASTEXITCODE

    $results.DlcPass = ($results.Dlc -eq 0)
    $results.SavePass = ($results.Save -eq 0)
    return [pscustomobject]$results
}

$suites = @()

Write-Host "=== dev build: $DevBuildDir ==="
$dev = Invoke-Suite "dev" $DevBuildDir "out/m7-dev"
$suites += $dev

if (-not $SkipInstalled) {
    $app = if ([System.IO.Path]::IsPathRooted($InstalledDir)) { Join-Path $InstalledDir "app" } else { Join-Path $root "$InstalledDir/app" }
    $payload = Join-Path $root "installer\out\payload"
    if (-not $SkipPayloadRefresh) {
        Write-Host "`n=== refreshing the installer payload from $DevBuildDir ==="
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "installer\tools\make_payload.ps1") -Clean `
            -SourceDir (Join-Path $root $DevBuildDir) 2>&1 | ForEach-Object { Write-Host $_ }
    }
    if (-not (Test-Path (Join-Path $payload "rb_blitz.exe"))) { throw "no payload at $payload (run without -SkipPayloadRefresh)" }
    if (Test-Path $app) { Remove-Item -Recurse -Force $app }
    New-Item -ItemType Directory -Force -Path $app | Out-Null
    Copy-Item (Join-Path $payload "*") $app -Force
    Remove-Item (Join-Path $app "rb_blitz.toml") -Force -ErrorAction SilentlyContinue
    Write-Host "`n=== installed build: $app ($((Get-ChildItem $app -File).Count) files, no rb_blitz.toml) ==="
    $installed = Invoke-Suite "installed" $app "$InstalledDir/suites"
    $suites += $installed
}

# ----------------------------------------------------------------- verdict ---

Write-Host "`n=== alpha acceptance ==="
$suites | Select-Object Label, @{n = "DLC"; e = { if ($_.DlcPass) { "PASS" } else { "FAIL" } } },
    @{n = "Saving"; e = { if ($_.SavePass) { "PASS" } else { "FAIL" } } } |
    Format-Table -AutoSize | Out-String | Write-Host

$summary = [pscustomobject]@{
    ExpectedSong = $ExpectedSong; Steps = $Steps; GameRoot = $GameRoot
    Suites = $suites
}
$summary | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $root "out/alpha-acceptance.json")

$failed = @($suites | Where-Object { -not ($_.DlcPass -and $_.SavePass) })
if ($failed.Count -gt 0) {
    Write-Host ("alpha: FAIL ({0} suite(s))" -f ($failed.Label -join ", "))
    exit 1
}
Write-Host "alpha: PASS"
exit 0
