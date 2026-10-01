# Extract the retail 360 assets out of the game's ARK archives.
#
# The game keeps its content in one Harmonix archive pair, `gen\main_xbox.hdr`
# plus `gen\main_xbox_0.ark`, and the optional Rock Band Blitz Ultimate payload
# ships an overlay pair under `ultimate\gen\`. This script unpacks both with
# scripts/hmx_ark.py: the base archive first, then the Ultimate patch over it in
# place, so the result is the same file tree the title sees with Ultimate
# installed. A per-archive index is written beside the tree as JSON.
#
# Everything this writes is retail content and therefore lands in a gitignored
# directory (`/extracted/`). Nothing here is ever committed.
#
# Usage:
#   .\scripts\extract_assets.ps1                      # game\ -> extracted\
#   .\scripts\extract_assets.ps1 -Clean               # wipe the tree first
#   .\scripts\extract_assets.ps1 -SkipUltimate        # base archive only
#   .\scripts\extract_assets.ps1 -GameRoot D:\rb_blitz -OutDir D:\rb_blitz_assets
param(
    [string]$GameRoot = "game",
    [string]$OutDir = "extracted",
    [string]$Python = "python",
    [switch]$SkipUltimate,
    [switch]$Clean,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $PSScriptRoot "hmx_ark.py"
$gameRootAbs = [System.IO.Path]::GetFullPath((Join-Path $root $GameRoot))
$outAbs = [System.IO.Path]::GetFullPath((Join-Path $root $OutDir))

function Invoke-Ark([string]$Hdr, [string]$Manifest, [switch]$Overwrite) {
    $toolArgs = @($tool, "extract", "--hdr", $Hdr, "--out", $outAbs, "--manifest", $Manifest)
    if ($Overwrite) { $toolArgs += "--overwrite" }
    if ($DryRun) { $toolArgs += "--dry-run" }
    & $Python @toolArgs
    if ($LASTEXITCODE -ne 0) { throw "ark extraction failed: $Hdr" }
}

$mainHdr = Join-Path $gameRootAbs "gen\main_xbox.hdr"
if (-not (Test-Path $mainHdr)) {
    throw "no retail archive at $mainHdr - put the dumped game in $gameRootAbs, or pass -GameRoot"
}

if ($Clean -and (Test-Path $outAbs) -and -not $DryRun) {
    Write-Host "clearing $outAbs"
    Remove-Item -Recurse -Force $outAbs
}
New-Item -ItemType Directory -Force -Path $outAbs | Out-Null

Write-Host "base archive -> $outAbs"
$mainManifest = Join-Path $outAbs "_ark_main_xbox.json"
Invoke-Ark -Hdr $mainHdr -Manifest $mainManifest

$patchHdr = Join-Path $gameRootAbs "ultimate\gen\patch_xbox.hdr"
if (-not $SkipUltimate -and (Test-Path $patchHdr)) {
    Write-Host "ultimate patch over the base tree"
    $patchManifest = Join-Path $outAbs "_ark_patch_xbox.json"
    Invoke-Ark -Hdr $patchHdr -Manifest $patchManifest -Overwrite
}

if ($DryRun) { return }

$base = (Get-Content $mainManifest -Raw | ConvertFrom-Json).files.path
$patch = @()
if (-not $SkipUltimate -and (Test-Path (Join-Path $outAbs "_ark_patch_xbox.json"))) {
    $patch = (Get-Content (Join-Path $outAbs "_ark_patch_xbox.json") -Raw | ConvertFrom-Json).files.path
}
$added = @($patch | Where-Object { $_ -notin $base })
$overrode = @($patch | Where-Object { $_ -in $base })

$files = Get-ChildItem $outAbs -Recurse -File
$bytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ""
Write-Host ("{0}: {1} files, {2:N1} MiB" -f $outAbs, $files.Count, ($bytes / 1MB))
Write-Host ("  base archive        {0} entries" -f $base.Count)
if ($patch.Count -gt 0) {
    Write-Host ("  ultimate overlay    {0} entries ({1} added, {2} replaced)" -f `
        $patch.Count, $added.Count, $overrode.Count)
}
Write-Host "  layout: the ark root itself (config\, songs\, ui\, ... plus the two loose shader blobs)"
