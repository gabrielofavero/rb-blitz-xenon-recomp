<#
.SYNOPSIS
Snapshots the recompiled build into installer/out/payload, with a manifest.

.DESCRIPTION
The installer places exactly the files of the recompiled build that the game
needs to run: the executable, the two runtime DLLs it loads, and the Microsoft
Visual C++ runtime DLLs it imports. Nothing else is quoted from the build tree -
in particular rb_blitz.toml is a developer profile, not a shipped file, and the
*.pdb / *d.dll files there belong to a debug build.

The list below is not a guess: the C++ runtime DLLs are the ones llvm-readobj
reports as imports of the three binaries, and the script re-derives that from the
binaries it copies, so a build that starts needing another DLL fails here instead
of on the user's machine.

The snapshot is written as payload-manifest.toml, in the same fingerprint format
config/game_fingerprints.toml uses, because that is what the native helper reads
back (see src/install.cpp VerifyPayloadTree).

.PARAMETER SourceDir
Build tree to snapshot. Defaults to out\build\win-amd64-release in this checkout.

.PARAMETER PayloadDir
Where to write the snapshot. Defaults to installer\out\payload.

.PARAMETER CrtDir
Directory holding the app-local Microsoft Visual C++ runtime DLLs. Defaults to
the newest one installed with Visual Studio. The Installer/onecore variants of
that directory contain a different DLL set and must not be used.

.PARAMETER Version
Version recorded in the manifest. Defaults to [installer] version in
config/pins.toml.

.PARAMETER Zip
Also write installer\out\payload.zip, the release asset that build.ps1
-PayloadUrl downloads. The manifest sits at the archive root, which is where the
helper expects it.

.PARAMETER Clean
Empty the payload directory first, so files this script no longer copies cannot
survive from an earlier run.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File installer\tools\make_payload.ps1 -Clean -Zip
#>
[CmdletBinding()]
param(
    [string] $SourceDir,
    [string] $PayloadDir,
    [string] $CrtDir,
    [string] $Version,
    [string] $ZipPath,
    [switch] $Zip,
    [switch] $Clean,
    [switch] $SkipDependencyCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$installerDir = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $installerDir

if (-not $SourceDir) { $SourceDir = Join-Path $repoRoot 'out\build\win-amd64-release' }
if (-not $PayloadDir) { $PayloadDir = Join-Path $installerDir 'out\payload' }
if (-not $ZipPath) { $ZipPath = Join-Path $installerDir 'out\payload.zip' }

function Get-PinsVersion {
    param([string] $Path)
    if (-not (Test-Path -LiteralPath $Path)) { return '' }
    $section = ''
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') { $section = $Matches[1]; continue }
        if ($section -ne 'installer') { continue }
        if ($trimmed -match '^version\s*=\s*"([^"]*)"') { return $Matches[1] }
    }
    return ''
}

function Get-DefaultCrtDir {
    if ($env:VCToolsRedistDir) {
        $candidate = Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT'
        if (Test-Path -LiteralPath (Join-Path $candidate 'vcruntime140.dll')) { return $candidate }
    }
    $patterns = @(
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT',
        'C:\Program Files\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT'
    )
    $found = @()
    foreach ($pattern in $patterns) {
        $found += Get-ChildItem -Path $pattern -Directory -ErrorAction SilentlyContinue
    }
    if ($found.Count -eq 0) { return '' }
    # Newest toolset wins; the directory name is the redist version.
    return ($found | Sort-Object -Property Name -Descending | Select-Object -First 1).FullName
}

function Get-PeImports {
    param([string] $Path, [string] $Reader)
    $imports = New-Object System.Collections.Generic.HashSet[string]
    $output = & $Reader --coff-imports $Path 2>$null
    foreach ($line in $output) {
        if ($line -match 'Name:\s+([^\s]+\.dll)\s*$') {
            [void] $imports.Add($Matches[1].ToLowerInvariant())
        }
    }
    return $imports
}

if (-not (Test-Path -LiteralPath $SourceDir)) {
    throw "build tree not found: $SourceDir. Build it first (see docs/build-and-run.md)."
}
if (-not $Version) { $Version = Get-PinsVersion (Join-Path $installerDir 'config\pins.toml') }
if (-not $Version) { $Version = 'unknown' }

if (-not $CrtDir) { $CrtDir = Get-DefaultCrtDir }
if (-not $CrtDir -or -not (Test-Path -LiteralPath $CrtDir)) {
    throw "app-local C++ runtime directory not found. Install the Visual C++ runtime files (Visual Studio Build Tools, 'VC++ 2022 redistributable') or pass -CrtDir."
}

# name -> directory it comes from. The runtime DLLs are the ones the binaries
# import (checked below); the rest of Machine.VC143.CRT is for other languages
# and for WinRT/ConcRT, which nothing here uses.
$wanted = [ordered]@{
    'rb_blitz.exe'             = $SourceDir
    'rexruntime.dll'           = $SourceDir
    'rexgpu-xenos.dll'         = $SourceDir
    'msvcp140.dll'             = $CrtDir
    'msvcp140_atomic_wait.dll' = $CrtDir
    'vcruntime140.dll'         = $CrtDir
    'vcruntime140_1.dll'       = $CrtDir
}

$missing = @()
foreach ($entry in $wanted.GetEnumerator()) {
    $path = Join-Path $entry.Value $entry.Key
    if (-not (Test-Path -LiteralPath $path)) { $missing += $path }
}
if ($missing.Count -gt 0) {
    throw ("missing input files:`n  " + ($missing -join "`n  "))
}

if ($Clean -and (Test-Path -LiteralPath $PayloadDir)) {
    Remove-Item -LiteralPath $PayloadDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $PayloadDir | Out-Null

$entries = @()
foreach ($entry in $wanted.GetEnumerator()) {
    $source = Join-Path $entry.Value $entry.Key
    $destination = Join-Path $PayloadDir $entry.Key
    Copy-Item -LiteralPath $source -Destination $destination -Force
    $item = Get-Item -LiteralPath $destination
    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    $entries += [pscustomobject]@{
        Name   = $entry.Key
        Size   = $item.Length
        Sha256 = $hash
        Full   = $destination
    }
}

# Every DLL the payload imports has to be either part of the payload or part of
# Windows; anything else is the redistributable being absent from the snapshot.
if (-not $SkipDependencyCheck) {
    $reader = $null
    foreach ($candidate in @('llvm-readobj.exe', 'llvm-readobj')) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) { $reader = $command.Source; break }
    }
    if (-not $reader -and (Test-Path -LiteralPath 'C:\Program Files\LLVM\bin\llvm-readobj.exe')) {
        $reader = 'C:\Program Files\LLVM\bin\llvm-readobj.exe'
    }
    if (-not $reader) {
        Write-Warning 'llvm-readobj was not found, so the payload''s DLL imports were not checked.'
    }
    else {
        $present = New-Object System.Collections.Generic.HashSet[string]
        foreach ($item in $entries) { [void] $present.Add($item.Name.ToLowerInvariant()) }
        $systemDir = Join-Path $env:WINDIR 'System32'
        $problems = @()
        foreach ($item in $entries) {
            foreach ($import in (Get-PeImports -Path $item.Full -Reader $reader)) {
                if ($import.StartsWith('api-ms-win-') -or $import.StartsWith('ext-ms-')) { continue }
                if ($present.Contains($import)) { continue }
                if (Test-Path -LiteralPath (Join-Path $systemDir $import)) { continue }
                $problems += "$($item.Name) imports $import, which is neither in the payload nor a Windows DLL"
            }
        }
        if ($problems.Count -gt 0) {
            throw ("the payload would not run:`n  " + ($problems -join "`n  "))
        }
    }
}

$manifest = New-Object System.Collections.Generic.List[string]
$manifest.Add('# Generated by tools/make_payload.ps1 - do not edit.')
$manifest.Add('# Snapshot of the recompiled build the installer places; read back by')
$manifest.Add('# rb_blitz_setup_helper.exe (verify-payload). Same format as')
$manifest.Add('# config/game_fingerprints.toml, which is what the runtime checks.')
$manifest.Add('schema_version = 1')
$manifest.Add('')
$manifest.Add('[game]')
$manifest.Add("name = `"rb_blitz $Version (win-amd64)`"")
foreach ($item in $entries) {
    $manifest.Add('')
    $manifest.Add('[[files]]')
    $manifest.Add("role = `"payload-$($item.Name)`"")
    $manifest.Add("path = `"$($item.Name)`"")
    $manifest.Add("size = $($item.Size)")
    $manifest.Add("sha256 = `"$($item.Sha256)`"")
}
$manifestPath = Join-Path $PayloadDir 'payload-manifest.toml'
# LF and a trailing newline: the manifest is compared and re-read, not diffed by
# eye, but a stable byte-for-byte result keeps it reproducible.
[System.IO.File]::WriteAllText($manifestPath, (($manifest -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding $false))

$total = ($entries | Measure-Object -Property Size -Sum).Sum
Write-Host ("payload snapshot: {0} files, {1:N1} MiB -> {2}" -f $entries.Count, ($total / 1MB), $PayloadDir)
foreach ($item in $entries) {
    Write-Host ("  {0,-26} {1,12:N0} bytes  {2}" -f $item.Name, $item.Size, $item.Sha256.Substring(0, 16))
}

if ($Zip) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ZipPath) | Out-Null
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    # CreateFromDirectory puts the contents at the archive root, which is where
    # the helper looks for payload-manifest.toml. Zip entries carry the source
    # mtimes, so the archive is not byte-reproducible: pin the sha256 printed by
    # the run that produced the published asset.
    [System.IO.Compression.ZipFile]::CreateFromDirectory($PayloadDir, $ZipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    # Not $zip: variable names are case-insensitive, so that would be the [switch] $Zip parameter.
    $archive = Get-Item -LiteralPath $ZipPath
    $zipHash = (Get-FileHash -LiteralPath $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host ''
    Write-Host "release asset: $ZipPath"
    Write-Host ("  size   = {0}" -f $archive.Length)
    Write-Host ("  sha256 = {0}" -f $zipHash)
    Write-Host '  Use these with build.ps1 -PayloadUrl/-PayloadSha256, or [payload] in config/pins.toml.'
}

Write-Host "manifest: $manifestPath"
