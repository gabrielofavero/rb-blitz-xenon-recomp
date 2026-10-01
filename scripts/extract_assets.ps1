# Extract the retail Rock Band Blitz assets out of the game's ARK archives.
#
# The game keeps its content in one Harmonix archive pair, and the two console
# builds differ only in the file names and the platform suffix inside them:
#
#   360   gen\main_xbox.hdr    + gen\main_xbox_0.ark
#         ultimate\gen\patch_xbox.hdr + _0.ark      (the Ultimate payload)
#   PS3   gen\main_ps3.hdr     + gen\main_ps3_0.ark
#         gen\patch_ps3.hdr    + _0.ark             (the title update)
#
# The archive format is identical - same version 6 layout, same header cipher,
# same texture envelopes (docs/assets.md#ps3) - so both are unpacked by the same
# scripts/hmx_ark.py. This script picks the pair and then unpacks the base
# archive followed by the overlay over it in place, so the result is the file
# tree the title reads with its update installed. A per-archive index is written
# beside the tree as JSON.
#
# Everything this writes is retail content and therefore lands in a gitignored
# directory. Nothing here is ever committed.
#
# Usage:
#   .\scripts\extract_assets.ps1                          # game\ -> extracted\
#   .\scripts\extract_assets.ps1 -Clean                   # wipe the tree first
#   .\scripts\extract_assets.ps1 -SkipOverlay             # base archive only
#   .\scripts\extract_assets.ps1 -Platform ps3 -GameRoot `
#       "C:\Games\Emulators\RPCS3\dev_hdd0\game\NPUB30749\USRDIR"      # -> extracted-ps3\
#   .\scripts\extract_assets.ps1 -GameRoot D:\rb_blitz -OutDir D:\rb_blitz_assets
param(
    [string]$GameRoot = "game",
    [string]$OutDir,
    [ValidateSet("auto", "xbox", "ps3")]
    [string]$Platform = "auto",
    [string]$Python = "python",
    # The overlay is the Ultimate payload on 360 and the title update on PS3;
    # both are optional and both are skipped by this switch.
    [switch]$SkipOverlay,
    # Kept as an alias: earlier revisions of this script only knew about Ultimate.
    [switch]$SkipUltimate,
    [switch]$Clean,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $PSScriptRoot "hmx_ark.py"

# Join-Path concatenates rather than replacing, so a -GameRoot or -OutDir that is
# already absolute (and may point at another drive entirely) has to be left alone.
function Resolve-Input([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $root $Path))
}
$gameRootAbs = Resolve-Input $GameRoot

# An overlay is optional on both platforms and is not the same file on each: the
# 360 one is the Ultimate payload beside the game root, the PS3 one is the title
# update in gen\ next to the base archive.
$layouts = @{
    xbox = @{ Suffix = "xbox"; Base = "gen\main_xbox.hdr"; Overlay = "ultimate\gen\patch_xbox.hdr" }
    ps3  = @{ Suffix = "ps3";  Base = "gen\main_ps3.hdr";  Overlay = "gen\patch_ps3.hdr" }
}

if ($Platform -eq "auto") {
    foreach ($name in @("xbox", "ps3")) {
        if (Test-Path (Join-Path $gameRootAbs $layouts[$name].Base)) { $Platform = $name; break }
    }
    if ($Platform -eq "auto") {
        throw "no retail archive under $gameRootAbs (looked for gen\main_xbox.hdr and gen\main_ps3.hdr); pass -GameRoot"
    }
    Write-Host "detected the $Platform build"
}

$layout = $layouts[$Platform]
$mainHdr = Join-Path $gameRootAbs $layout.Base
if (-not (Test-Path $mainHdr)) {
    throw "no $Platform archive at $mainHdr - check -GameRoot"
}

# Default output is per platform, so a PS3 extraction cannot overwrite a 360 one.
if (-not $OutDir) {
    $OutDir = if ($Platform -eq "xbox") { "extracted" } else { "extracted-$Platform" }
}
$outAbs = Resolve-Input $OutDir
$skipOverlay = $SkipOverlay -or $SkipUltimate

function Invoke-Ark([string]$Hdr, [string]$Manifest, [switch]$Overwrite) {
    $toolArgs = @($tool, "extract", "--hdr", $Hdr, "--out", $outAbs, "--manifest", $Manifest)
    if ($Overwrite) { $toolArgs += "--overwrite" }
    if ($DryRun) { $toolArgs += "--dry-run" }
    & $Python @toolArgs
    if ($LASTEXITCODE -ne 0) { throw "ark extraction failed: $Hdr" }
}

if ($Clean -and (Test-Path $outAbs) -and -not $DryRun) {
    Write-Host "clearing $outAbs"
    Remove-Item -Recurse -Force $outAbs
}
New-Item -ItemType Directory -Force -Path $outAbs | Out-Null

Write-Host "base archive ($Platform) -> $outAbs"
$mainManifest = Join-Path $outAbs ("_ark_main_{0}.json" -f $layout.Suffix)
Invoke-Ark -Hdr $mainHdr -Manifest $mainManifest

$patchHdr = Join-Path $gameRootAbs $layout.Overlay
if (-not $skipOverlay -and (Test-Path $patchHdr)) {
    Write-Host "overlay ($($layout.Overlay)) over the base tree"
    $patchManifest = Join-Path $outAbs ("_ark_patch_{0}.json" -f $layout.Suffix)
    Invoke-Ark -Hdr $patchHdr -Manifest $patchManifest -Overwrite
}

if ($DryRun) { return }

$base = (Get-Content $mainManifest -Raw | ConvertFrom-Json).files.path
$patch = @()
$patchManifestPath = Join-Path $outAbs ("_ark_patch_{0}.json" -f $layout.Suffix)
if (-not $skipOverlay -and (Test-Path $patchManifestPath)) {
    $patch = (Get-Content $patchManifestPath -Raw | ConvertFrom-Json).files.path
}
$added = @($patch | Where-Object { $_ -notin $base })
$overrode = @($patch | Where-Object { $_ -in $base })

$files = Get-ChildItem $outAbs -Recurse -File
$bytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ""
Write-Host ("{0}: {1} files, {2:N1} MiB" -f $outAbs, $files.Count, ($bytes / 1MB))
Write-Host ("  base archive        {0} entries" -f $base.Count)
if ($patch.Count -gt 0) {
    Write-Host ("  overlay             {0} entries ({1} added, {2} replaced)" -f `
        $patch.Count, $added.Count, $overrode.Count)
}
Write-Host "  layout: the ark root itself (config\, songs\, ui\, ... plus the loose shader blobs)"
