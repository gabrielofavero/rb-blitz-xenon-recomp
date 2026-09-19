<#
.SYNOPSIS
    Applies the tracked patches from patches/rexglue-sdk/ to the pinned SDK checkout.

.DESCRIPTION
    `rexglue-sdk/` is an unmodified upstream submodule pinned by the parent
    gitlink, so local SDK fixes cannot be committed inside it (a gitlink to a
    local-only commit breaks a fresh clone). They live in `patches/rexglue-sdk/`
    instead and are applied on top of the pinned checkout.

    The script is idempotent: for every patch it first asks Git whether the patch
    is already applied (`git apply --reverse --check`), then whether it applies
    cleanly (`git apply --check`), and only then applies it. Patches are applied in
    file-name order.

    Re-run after `git submodule update`, a fresh clone, or any checkout that
    rewrites the SDK tree. See patches/README.md and docs/build-and-run.md.

    The script also audits the SDK working tree. `.gitmodules` sets
    `submodule.rexglue-sdk.ignore = dirty` so that the applied patches stop
    showing up as a modified submodule in the parent repository - which also
    means the parent's `git status` no longer reports work-tree edits inside the
    SDK at all (a moved gitlink is still reported). The audit compensates: every
    modified SDK file must match a patch in this set, otherwise it is listed as
    UNEXPECTED and the script exits non-zero.

.EXAMPLE
    .\scripts\apply_sdk_patches.ps1
    .\scripts\apply_sdk_patches.ps1 -Check
#>
[CmdletBinding()]
param(
    # Parent repository root. Defaults to the directory that contains scripts/.
    [string]$RepoRoot,

    # Submodule directory that receives the patches (relative to RepoRoot).
    [string]$SdkDir = 'rexglue-sdk',

    # Directory holding per-submodule patch sets (relative to RepoRoot).
    [string]$PatchRoot = 'patches',

    # Report what would happen without touching the working tree.
    [switch]$Check
)

$ErrorActionPreference = 'Continue'

if (-not $RepoRoot) {
    # $PSScriptRoot is not always populated while parameters are bound.
    $scriptDir = $PSScriptRoot
    if (-not $scriptDir) { $scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition }
    $RepoRoot = Split-Path -Parent $scriptDir
}

function Invoke-Git {
    param([string[]]$Arguments)
    $output = & git @Arguments 2>&1
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output   = @($output)
    }
}

function Get-DiffSections {
    # Split unified-diff text into sections keyed by the b/ path. The "index"
    # line is dropped: its abbreviation length follows core.abbrev, so the same
    # change can be spelled two ways and still be the same change.
    param([string[]]$Lines)

    $sections = @{}
    $current = $null
    $buffer = New-Object System.Collections.Generic.List[string]

    foreach ($line in $Lines) {
        $text = "$line"
        if ($text.StartsWith('diff --git ')) {
            if ($current) { $sections[$current] = ($buffer -join "`n") }
            $buffer.Clear()
            $current = $null
            if ($text -match '^diff --git a/(.+?) b/(.+)$') { $current = $matches[2] }
            continue
        }
        if (-not $current) { continue }
        if ($text.StartsWith('index ')) { continue }
        $buffer.Add($text.TrimEnd())
    }
    if ($current) { $sections[$current] = ($buffer -join "`n") }

    return $sections
}

$sdkPath = Join-Path $RepoRoot $SdkDir
$patchPath = Join-Path $RepoRoot (Join-Path $PatchRoot $SdkDir)

if (-not (Test-Path -LiteralPath (Join-Path $sdkPath '.git'))) {
    Write-Error "SDK submodule is not initialised: $sdkPath (run: git submodule update --init --recursive $SdkDir)"
    exit 1
}

$head = Invoke-Git @('-c', 'safe.directory=*', '-C', $sdkPath, 'rev-parse', 'HEAD')
$describe = Invoke-Git @('-c', 'safe.directory=*', '-C', $sdkPath, 'describe', '--tags', '--always')
$pinned = Invoke-Git @('-c', 'safe.directory=*', '-C', $RepoRoot, 'ls-tree', 'HEAD', '--', $SdkDir)

$headSha = ($head.Output -join '').Trim()
Write-Host "SDK checkout : $SdkDir @ $headSha ($($describe.Output -join ''))"
if ($pinned.ExitCode -eq 0 -and $pinned.Output.Count -gt 0) {
    if ($pinned.Output[0] -match '^\d+\s+commit\s+([0-9a-f]{40})\s') {
        $pinnedSha = $matches[1]
        if ($pinnedSha -ne $headSha) {
            Write-Warning "SDK HEAD differs from the gitlink recorded in the parent commit ($pinnedSha). Patches below may not apply; expect to update them."
        } else {
            Write-Host "Pinned as    : $pinnedSha (matches the parent gitlink)"
        }
    }
}

$patches = @()
if (Test-Path -LiteralPath $patchPath -PathType Container) {
    $patches = @(Get-ChildItem -LiteralPath $patchPath -Filter '*.patch' -File | Sort-Object Name)
} else {
    Write-Warning "No patch set directory at $patchPath - any SDK modification below is UNEXPECTED."
}
if ($patches.Count -eq 0) {
    Write-Host "Patch set          : none found in $patchPath"
}

$applied = @()
$current = @()
$failed = @()

foreach ($patch in $patches) {
    $reverse = Invoke-Git @('-c', 'safe.directory=*', '-C', $sdkPath, 'apply', '--reverse', '--check', $patch.FullName)
    if ($reverse.ExitCode -eq 0) {
        $current += $patch.Name
        continue
    }

    $forward = Invoke-Git @('-c', 'safe.directory=*', '-C', $sdkPath, 'apply', '--check', $patch.FullName)
    if ($forward.ExitCode -ne 0) {
        $failed += [pscustomobject]@{ Patch = $patch.Name; Detail = ($forward.Output -join ' ') }
        continue
    }

    if ($Check) {
        $applied += "$($patch.Name) (would apply)"
        continue
    }

    $result = Invoke-Git @('-c', 'safe.directory=*', '-C', $sdkPath, 'apply', $patch.FullName)
    if ($result.ExitCode -eq 0) {
        $applied += $patch.Name
    } else {
        $failed += [pscustomobject]@{ Patch = $patch.Name; Detail = ($result.Output -join ' ') }
    }
}

Write-Host ''
Write-Host "Patches considered : $($patches.Count)"
Write-Host "Already applied    : $($current.Count)"
foreach ($item in $current) { Write-Host "    $item" }
Write-Host "Applied now        : $($applied.Count)"
foreach ($item in $applied) { Write-Host "    $item" }
Write-Host "Failed             : $($failed.Count)"
foreach ($item in $failed) { Write-Host "    $($item.Patch): $($item.Detail)" }

if ($failed.Count -gt 0) {
    Write-Host ''
    Write-Host 'A patch that neither applies nor reverse-applies usually means the SDK pin moved,'
    Write-Host 'or the working tree was already edited by hand. Inspect with:'
    Write-Host "    git -C $SdkDir diff"
}

$patchSections = @{}
foreach ($patch in $patches) {
    $sections = Get-DiffSections -Lines @(Get-Content -LiteralPath $patch.FullName)
    foreach ($key in $sections.Keys) { $patchSections[$key] = $sections[$key] }
}

$status = Invoke-Git @('-c', 'safe.directory=*', '-C', $sdkPath, 'status', '--porcelain')
$accounted = @()
$uncovered = @()
$untracked = @()

foreach ($raw in $status.Output) {
    if ($null -eq $raw) { continue }
    $line = "$raw"
    if ($line.Trim().Length -eq 0) { continue }

    $entryPath = $line.Substring(3).Trim()
    if ($line.StartsWith('??')) { $untracked += $entryPath; continue }
    if ($entryPath.Contains(' -> ')) { $entryPath = ($entryPath -split ' -> ')[-1] }

    if (-not $patchSections.ContainsKey($entryPath)) {
        $uncovered += "$entryPath (not touched by any patch)"
        continue
    }

    $live = Invoke-Git @('-c', 'safe.directory=*', '-C', $sdkPath, 'diff', 'HEAD', '--', $entryPath)
    $liveSections = Get-DiffSections -Lines @($live.Output)
    if ($liveSections.ContainsKey($entryPath) -and $liveSections[$entryPath] -eq $patchSections[$entryPath]) {
        $accounted += $entryPath
    } else {
        $uncovered += "$entryPath (differs from the patch set)"
    }
}

Write-Host ''
Write-Host "SDK work tree      : $($accounted.Count) patched, $($uncovered.Count) UNEXPECTED, $($untracked.Count) untracked"
foreach ($item in $accounted) { Write-Host "    patched    : $item" }
foreach ($item in $uncovered) { Write-Host "    UNEXPECTED : $item" }
foreach ($item in $untracked) { Write-Host "    untracked  : $item" }

if ($uncovered.Count -gt 0) {
    Write-Host ''
    Write-Host 'These live inside the submodule, where submodule.ignore=dirty hides them from'
    Write-Host "the parent's git status. Fold each one into a patch under $(Join-Path $PatchRoot $SdkDir)"
    Write-Host '(see patches/README.md), or discard it with:'
    Write-Host "    git -C $SdkDir checkout -- ."
}

if ($failed.Count -gt 0 -or $uncovered.Count -gt 0) { exit 1 }
exit 0
