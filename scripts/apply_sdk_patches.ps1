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

$sdkPath = Join-Path $RepoRoot $SdkDir
$patchPath = Join-Path $RepoRoot (Join-Path $PatchRoot $SdkDir)

if (-not (Test-Path -LiteralPath $patchPath -PathType Container)) {
    Write-Host "No patch set at $patchPath - nothing to do."
    exit 0
}
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

$patches = @(Get-ChildItem -LiteralPath $patchPath -Filter '*.patch' -File | Sort-Object Name)
if ($patches.Count -eq 0) {
    Write-Host "No *.patch files in $patchPath - nothing to do."
    exit 0
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
    exit 1
}
exit 0
