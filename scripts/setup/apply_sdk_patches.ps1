<#
.SYNOPSIS
    Replay and audit the pinned SDK patch stack, including nested dependencies.
.DESCRIPTION
    Uses the shared Python replay engine before writing any SDK file. Requires
    Python 3.10+ on PATH. Accepts CRLF equivalents and preserves/reports unrelated
    untracked files for Windows development; tracked conflicts and pin drift fail.
    -Check audits without writing. See patches/README.md.
#>
[CmdletBinding()]
param(
    [string]$RepoRoot,
    [string]$SdkDir = 'rexglue-sdk',
    [string]$PatchRoot = 'patches',
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
if (-not $RepoRoot) {
    $RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
$engine = Join-Path $PSScriptRoot 'apply_sdk_patches.py'
$arguments = @($engine, '--repo-root', $RepoRoot, '--sdk-dir', $SdkDir,
               '--patch-root', $PatchRoot, '--windows-compatible')
if ($Check) { $arguments += '--check' }
& python @arguments
exit $LASTEXITCODE
