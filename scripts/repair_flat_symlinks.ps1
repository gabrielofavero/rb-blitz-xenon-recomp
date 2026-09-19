<#
.SYNOPSIS
    Restores ReXGlue SDK submodule files that Windows checked out flattened.

.DESCRIPTION
    Some ReXGlue SDK thirdparty submodules (libmspack, o1heap) track files as
    symbolic links (index mode 120000). This machine has no symlink privilege
    (the shell is not elevated and Windows Developer Mode is off), so Git leaves
    `core.symlinks=false` and materialises every link as a *small plain file
    containing the link text*.

    That is fatal for `thirdparty/libmspack/cabextract/mspack/lzxd.c`: the SDK
    compiles exactly that path (`rexglue-sdk/thirdparty/CMakeLists.txt` sets
    `MSPACK_DIR` to `libmspack/cabextract/mspack`), so a flattened tree breaks the
    build with a nonsense compile error instead of a missing-file error.

    This script walks every initialised submodule, finds the mode-120000 entries,
    and makes each working-tree file contain the *contents of the link target*
    rather than the link text. It is idempotent: entries that already match their
    target are reported and left alone.

    By default it also marks exactly those paths `--skip-worktree`, so `git
    status` stays quiet about the unavoidable, harmless content difference. Use
    -NoIndexMarks to leave the index untouched, or -ClearMarks to undo the marks.

    Re-run after `git submodule update`, a fresh clone, or any checkout that
    rewrites the SDK tree. Background: docs/bringup-log.md B-001,
    docs/known-issues.md.

.EXAMPLE
    .\scripts\repair_flat_symlinks.ps1
    .\scripts\repair_flat_symlinks.ps1 -NoIndexMarks
    .\scripts\repair_flat_symlinks.ps1 -ClearMarks
#>
[CmdletBinding()]
param(
    # Parent repository root. Defaults to the directory that contains scripts/.
    [string]$RepoRoot,

    # Submodule whose status is printed as a summary (relative to RepoRoot).
    [string]$SdkDir = 'rexglue-sdk',

    # Repair files but do not touch the index.
    [switch]$NoIndexMarks,

    # Remove the --skip-worktree marks instead of adding them.
    [switch]$ClearMarks
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

function Get-FileSha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

if (-not (Test-Path -LiteralPath $RepoRoot -PathType Container)) {
    Write-Error "Repository root not found: $RepoRoot"
    exit 1
}
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path

# Every initialised submodule, recursively, plus the root repository itself.
$submodulePaths = @('')
$status = Invoke-Git @('-c', 'safe.directory=*', '-C', $RepoRoot, 'submodule', 'status', '--recursive')
foreach ($line in $status.Output) {
    if ($line -notmatch '^\s*([-+U])?([0-9a-f]{40})\s+(\S+)') { continue }
    $prefix = $matches[1]
    $path = $matches[3]
    if ($prefix -eq '-') {
        Write-Verbose "Skipping uninitialised submodule: $path"
        continue
    }
    $submodulePaths += $path
}

$checked = 0
$alreadyOk = @()
$repaired = @()
$realLinks = @()
$unrepairable = @()
$markTargets = @{}

foreach ($relative in $submodulePaths) {
    if ($relative) {
        $repoDir = Join-Path $RepoRoot $relative
    } else {
        $repoDir = $RepoRoot
    }
    if (-not (Test-Path -LiteralPath $repoDir -PathType Container)) { continue }

    $listed = Invoke-Git @('-c', 'safe.directory=*', '-C', $repoDir, '-c', 'core.quotePath=false', 'ls-files', '-s')
    if ($listed.ExitCode -ne 0) {
        Write-Warning "Could not list tracked files in $repoDir"
        continue
    }

    foreach ($entry in $listed.Output) {
        if ($entry -notmatch '^120000\s+([0-9a-f]+)\s+\d+\t(.+)$') { continue }
        $oid = $matches[1]
        $entryPath = $matches[2]
        $checked++

        $blob = Invoke-Git @('-c', 'safe.directory=*', '-C', $repoDir, 'cat-file', 'blob', $oid)
        if ($blob.ExitCode -ne 0) {
            $unrepairable += "$relative/$entryPath (link blob $oid unreadable)"
            continue
        }
        # A symlink blob is the link text, which is a single line.
        $linkText = ($blob.Output -join "`n").TrimEnd("`r", "`n")

        $entryDir = Join-Path $repoDir (Split-Path -Parent $entryPath)
        $entryAbs = Join-Path $repoDir $entryPath
        $targetAbs = [System.IO.Path]::GetFullPath((Join-Path $entryDir $linkText))

        if (-not (Test-Path -LiteralPath $targetAbs -PathType Leaf)) {
            $unrepairable += "$relative/$entryPath (link target missing: $linkText)"
            continue
        }

        $item = Get-Item -Force -LiteralPath $entryAbs -ErrorAction SilentlyContinue
        if ($item -and $item.LinkType) {
            $realLinks += "$relative/$entryPath"
            continue
        }

        if ($item -and (Get-FileSha256 -Path $entryAbs) -eq (Get-FileSha256 -Path $targetAbs)) {
            $alreadyOk += "$relative/$entryPath"
        } else {
            $parent = Split-Path -Parent $entryAbs
            if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
                New-Item -ItemType Directory -Path $parent -Force | Out-Null
            }
            Copy-Item -LiteralPath $targetAbs -Destination $entryAbs -Force
            $repaired += "$relative/$entryPath <- $linkText"
        }

        if (-not $markTargets.ContainsKey($repoDir)) { $markTargets[$repoDir] = @() }
        $markTargets[$repoDir] += ($entryPath -replace '\\', '/')
    }
}

$markedCount = 0
if (-not $NoIndexMarks -and $markTargets.Count -gt 0) {
    $flag = '--skip-worktree'
    if ($ClearMarks) { $flag = '--no-skip-worktree' }
    foreach ($repoDir in $markTargets.Keys) {
        $paths = $markTargets[$repoDir]
        $result = Invoke-Git (@('-c', 'safe.directory=*', '-C', $repoDir, 'update-index', $flag, '--') + $paths)
        if ($result.ExitCode -ne 0) {
            Write-Warning "update-index $flag failed in $repoDir"
            $result.Output | ForEach-Object { Write-Warning $_ }
        } else {
            $markedCount += $paths.Count
        }
    }
}

Write-Host ''
Write-Host "Flattened-symlink entries found: $checked"
Write-Host "  content already correct : $($alreadyOk.Count)"
Write-Host "  repaired from target    : $($repaired.Count)"
Write-Host "  genuine symlinks (skip) : $($realLinks.Count)"
Write-Host "  not repairable          : $($unrepairable.Count)"
foreach ($item in $repaired) { Write-Host "    repaired: $item" }
foreach ($item in $unrepairable) { Write-Host "    UNREPAIRABLE: $item" }

if ($NoIndexMarks) {
    Write-Host 'Index marks: skipped (-NoIndexMarks).'
} elseif ($ClearMarks) {
    Write-Host "Index marks removed for $markedCount paths."
} else {
    Write-Host "Index marks: $markedCount paths set --skip-worktree (undo with -ClearMarks)."
}

$sdk = Join-Path $RepoRoot $SdkDir
if (Test-Path -LiteralPath (Join-Path $sdk '.git')) {
    Write-Host ''
    Write-Host "git -C $SdkDir status --porcelain:"
    $sdkStatus = Invoke-Git @('-c', 'safe.directory=*', '-C', $sdk, 'status', '--porcelain')
    if ($sdkStatus.Output.Count -eq 0) {
        Write-Host '  (clean)'
    } else {
        $sdkStatus.Output | ForEach-Object { Write-Host "  $_" }
    }
}

if ($unrepairable.Count -gt 0) { exit 1 }
exit 0
