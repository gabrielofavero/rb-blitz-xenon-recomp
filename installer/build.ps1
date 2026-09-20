<#
.SYNOPSIS
Builds the Rock Band Blitz setup executable (rb_blitz_setup_helper.exe + setup.iss + ISCC).

.DESCRIPTION
One command, three artefacts: the native helper, the payload the installer puts on
disk, and the setup executable the user downloads. Nothing here is needed to play
the game - this script only builds the installer.

The recompiled build travels one of two ways, and this script has to be told which:

  * inside the setup executable (the default). The payload snapshot in
    out\payload is compressed into the setup exe, so the installer needs no
    network access at all. That snapshot is made by tools\make_payload.ps1 from a
    recompiled build tree, and is created automatically if it is missing.

  * downloaded at install time (-PayloadUrl -PayloadSha256). Nothing is embedded
    and the user's machine fetches the release asset instead. Pass -PayloadSize
    too when the size is known: the wizard uses it to check free space before it
    starts.

The Rock Band Blitz Ultimate mod is never embedded and never re-hosted; its
release URL lives in config\pins.toml and the user's copy is downloaded from
upstream, or supplied by the user, while the installer runs. See README.md.

.PARAMETER PayloadUrl
Download the recompiled build from this URL at install time instead of embedding
it. Requires -PayloadSha256.

.PARAMETER PayloadSha256
Lower-case hex SHA-256 of the file at -PayloadUrl. The installer refuses to
install a download that does not match it, so pass the real one.

.PARAMETER PayloadSize
Size of the file at -PayloadUrl in bytes. Optional; only used for the free-space
pre-check.

.PARAMETER PayloadVersion
Version string recorded in the install manifest for the recompiled build.
Defaults to the [installer] version in config\pins.toml.

.PARAMETER AllowUnverifiedPayload
Permit -PayloadUrl without -PayloadSha256. Only for testing a build of your own;
a published installer must name the hash of what it downloads.

.PARAMETER PayloadDir
Directory holding (or receiving) the payload snapshot. Defaults to out\payload.

.PARAMETER SourceDir
Recompiled build tree to snapshot. Defaults to out\build\win-amd64-release in
this checkout, which is what the emulator build produces.

.PARAMETER IsccPath
ISCC.exe to use. Discovered from the registry, PATH and the usual install
location when omitted.

.PARAMETER Preset
CMake preset that builds the helper. installer-release (clang++) by default;
installer-release-msvc uses cl.exe from a Visual Studio developer prompt.

.PARAMETER SkipTests
Do not run the installer helper's test suite.

.PARAMETER SkipPayload
Do not create the payload snapshot, even if it is missing. Use with -PayloadUrl,
or when the snapshot was made deliberately by tools\make_payload.ps1.

.PARAMETER RefreshPayload
Recreate the payload snapshot even if out\payload already has one.

.PARAMETER SkipArt
Do not refresh the wizard images (tools\make_art.ps1). The images are optional
and are not committed, so a failed download is not a build failure.

.PARAMETER SkipSetup
Build and test the helper only; do not compile the setup executable.

.EXAMPLE
powershell -ExecutionPolicy Bypass -NoProfile -File installer\build.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -NoProfile -File installer\build.ps1 -PayloadUrl https://example.invalid/rb_blitz-1.0.0.zip -PayloadSha256 0123abcd... -PayloadSize 52446752
#>
[CmdletBinding()]
param(
    [string] $PayloadUrl,
    [string] $PayloadSha256,
    [long]   $PayloadSize = 0,
    [string] $PayloadVersion,
    [switch] $AllowUnverifiedPayload,
    [string] $PayloadDir,
    [string] $SourceDir,
    [string] $IsccPath,
    [string] $Preset = 'installer-release',
    [switch] $SkipTests,
    [switch] $SkipPayload,
    [switch] $RefreshPayload,
    [switch] $SkipArt,
    [switch] $SkipSetup
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$installerDir = $PSScriptRoot
$repoRoot = Split-Path -Parent $installerDir

if (-not $PayloadDir) { $PayloadDir = Join-Path $installerDir 'out\payload' }
if (-not $SourceDir) { $SourceDir = Join-Path $repoRoot 'out\build\win-amd64-release' }

$generatedDir = Join-Path $installerDir 'out\generated'
$distDir = Join-Path $installerDir 'out\dist'
$artDir = Join-Path $installerDir 'assets'
$buildDir = Join-Path $installerDir "out\build\$Preset"
$payloadManifest = Join-Path $PayloadDir 'payload-manifest.toml'
$makePayload = Join-Path $installerDir 'tools\make_payload.ps1'
$makeArt = Join-Path $installerDir 'tools\make_art.ps1'
$setupScript = Join-Path $installerDir 'setup.iss'

function Write-Step {
    param([string] $Text)
    Write-Host ''
    Write-Host "== $Text" -ForegroundColor Cyan
}

# Native tools write progress and warnings to stderr, which PowerShell 5.1 turns
# into error records; with a Stop preference that would abort the build on a
# compiler warning. Run them with the preference relaxed and decide on the exit
# code instead. Output stays live on the console.
#
# CMake looks for CMakePresets.json in the current directory, and this project's
# presets are the installer's own (the emulator has a different set), so the
# working directory matters here.
function Invoke-Native {
    param(
        [string]   $Exe,
        [string[]] $Arguments,
        [string]   $What,
        [string]   $WorkingDirectory
    )

    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($WorkingDirectory) { Push-Location -LiteralPath $WorkingDirectory }
        try {
            & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host $_ }
            $exitCode = $LASTEXITCODE
        } finally {
            if ($WorkingDirectory) { Pop-Location }
        }
    } finally {
        $ErrorActionPreference = $saved
    }

    if ($exitCode -ne 0) {
        throw "$What failed with exit code $exitCode."
    }
}

function Find-Iscc {
    if ($IsccPath) {
        if (-not (Test-Path -LiteralPath $IsccPath)) { throw "ISCC.exe not found at $IsccPath." }
        return (Resolve-Path -LiteralPath $IsccPath).Path
    }

    $keys = @(
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1'
    )
    foreach ($key in $keys) {
        if (-not (Test-Path $key)) { continue }
        $location = (Get-ItemProperty -Path $key -Name InstallLocation -ErrorAction SilentlyContinue).InstallLocation
        if ($location) {
            $candidate = Join-Path $location 'ISCC.exe'
            if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
        }
    }

    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $fallback = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
    if (Test-Path -LiteralPath $fallback) { return (Resolve-Path -LiteralPath $fallback).Path }

    throw 'ISCC.exe not found. Install Inno Setup 6 (https://jrsoftware.org/isdl.php) or pass -IsccPath.'
}

function Get-SizeText {
    param([long] $Bytes)
    if ($Bytes -ge 1MB) { return ('{0:N0} bytes ({1:N1} MiB)' -f $Bytes, ($Bytes / 1MB)) }
    return ('{0:N0} bytes' -f $Bytes)
}

# --------------------------------------------------------------------------
# 1. the payload
# --------------------------------------------------------------------------
Write-Step 'Payload'

$embedPayload = -not $PayloadUrl
$embedArgs = @()

if ($PayloadUrl) {
    if (-not $PayloadSha256) {
        if (-not $AllowUnverifiedPayload) {
            throw ('-PayloadUrl needs -PayloadSha256: an installer that downloads its own build must ' +
                   'say which build it is. Pass -AllowUnverifiedPayload only to test a build of your own.')
        }
        Write-Warning 'downloading the payload without a hash: -AllowUnverifiedPayload is set.'
    } elseif ($PayloadSha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw "-PayloadSha256 must be 64 hex characters, got '$PayloadSha256'."
    }

    if ($PayloadSha256) { $embedArgs += @('--payload-sha256', $PayloadSha256.ToLowerInvariant()) }
    if ($PayloadSize -gt 0) { $embedArgs += @('--payload-size', $PayloadSize.ToString()) }
    if ($PayloadVersion) { $embedArgs += @('--payload-version', $PayloadVersion) }
    $embedArgs += @('--payload-url', $PayloadUrl)

    Write-Host "   mode    : downloaded at install time"
    Write-Host "   url     : $PayloadUrl"
    Write-Host "   sha256  : $(if ($PayloadSha256) { $PayloadSha256.ToLowerInvariant() } else { '(unverified)' })"
} else {
    $haveSnapshot = Test-Path -LiteralPath $payloadManifest
    if ($haveSnapshot -and -not $RefreshPayload) {
        Write-Host "   mode    : embedded in the setup executable"
        Write-Host "   snapshot: $PayloadDir (kept; pass -RefreshPayload to rebuild it)"
    } elseif ($SkipPayload) {
        if (-not $haveSnapshot) {
            throw "-SkipPayload was given but $payloadManifest does not exist."
        }
        Write-Host "   mode    : embedded in the setup executable"
        Write-Host "   snapshot: $PayloadDir"
    } else {
        if (-not (Test-Path -LiteralPath $SourceDir)) {
            throw ("no recompiled build to snapshot: $SourceDir does not exist. Build the emulator " +
                   'first, or pass -PayloadUrl to download the build at install time instead.')
        }
        Write-Host "   mode    : embedded in the setup executable"
        Write-Host "   snapshot: rebuilding from $SourceDir"
        $nativeArgs = @('-ExecutionPolicy', 'Bypass', '-NoProfile', '-File', $makePayload,
                        '-SourceDir', $SourceDir, '-PayloadDir', $PayloadDir, '-Clean')
        Invoke-Native -Exe 'powershell.exe' -Arguments $nativeArgs -What 'make_payload.ps1'
    }

    if ($PayloadVersion) { $embedArgs += @('--payload-version', $PayloadVersion) }
}

# --------------------------------------------------------------------------
# 2. the helper and its tests
# --------------------------------------------------------------------------
Write-Step 'Configuring and building the helper'

$configureArgs = @('--preset', $Preset)
# Always passed, even when empty: the value is cached, so leaving it out would
# keep the previous build's download pin and quietly ship the wrong payload.
$configureArgs += "-DRBBLITZ_EMBED_EXTRA_ARGS=$($embedArgs -join ';')"
Invoke-Native -Exe 'cmake' -Arguments $configureArgs -What 'cmake --preset' -WorkingDirectory $installerDir

$buildArgs = @('--build', '--preset', $Preset)
Invoke-Native -Exe 'cmake' -Arguments $buildArgs -What 'cmake --build --preset' -WorkingDirectory $installerDir

$helperExe = Join-Path $buildDir 'rb_blitz_setup_helper.exe'
$helperSize = 0
if (-not (Test-Path -LiteralPath $helperExe)) { throw "the build did not produce $helperExe." }
$helperSize = (Get-Item -LiteralPath $helperExe).Length
Write-Host "   helper  : $(Get-SizeText $helperSize) -> $(Join-Path $generatedDir 'rb_blitz_setup_helper.exe')"

if (-not $SkipTests) {
    Write-Step 'Running the installer tests'
    $testArgs = @('--preset', $Preset, '--output-on-failure')
    Invoke-Native -Exe 'ctest' -Arguments $testArgs -What 'ctest --preset' -WorkingDirectory $installerDir
}

if ($SkipSetup) {
    Write-Host ''
    Write-Host 'Skipped the setup executable (-SkipSetup).' -ForegroundColor Yellow
    exit 0
}

# --------------------------------------------------------------------------
# 3. the optional wizard images
# --------------------------------------------------------------------------
if (-not $SkipArt -and (Test-Path -LiteralPath $makeArt)) {
    Write-Step 'Wizard images (optional)'
    try {
        $artArgs = @('-ExecutionPolicy', 'Bypass', '-NoProfile', '-File', $makeArt, '-OutDir', $artDir)
        Invoke-Native -Exe 'powershell.exe' -Arguments $artArgs -What 'make_art.ps1'
        Write-Host "   images  : $artDir"
    } catch {
        Write-Warning "wizard images were not refreshed: $($_.Exception.Message)"
        Write-Warning 'the setup executable is built without them, which is a supported configuration.'
    }
}

# --------------------------------------------------------------------------
# 4. the setup executable
# --------------------------------------------------------------------------
Write-Step 'Compiling the setup executable'

$iscc = Find-Iscc
New-Item -ItemType Directory -Force -Path $distDir | Out-Null

$isccArgs = @('/Q',
              "/DGeneratedDir=$generatedDir",
              "/DDistDir=$distDir",
              "/DArtDir=$artDir",
              $setupScript)
Invoke-Native -Exe $iscc -Arguments $isccArgs -What 'ISCC'

$appVersion = ''
foreach ($line in Get-Content -LiteralPath (Join-Path $generatedDir 'pins.iss')) {
    if ($line -match '^#define AppVersion "([^"]*)"') { $appVersion = $Matches[1]; break }
}
if (-not $appVersion) { throw 'could not read AppVersion from the generated pins.iss.' }

$setupExe = Join-Path $distDir "RockBandBlitzSetup-$appVersion.exe"
if (-not (Test-Path -LiteralPath $setupExe)) { throw "ISCC reported success but $setupExe is missing." }

$setupInfo = Get-Item -LiteralPath $setupExe
$setupHash = (Get-FileHash -LiteralPath $setupExe -Algorithm SHA256).Hash.ToLowerInvariant()

Write-Host ''
Write-Host 'Built' -ForegroundColor Green
Write-Host ('   setup   : {0}' -f $setupInfo.FullName)
Write-Host ('   size    : {0}' -f (Get-SizeText $setupInfo.Length))
Write-Host ('   sha256  : {0}' -f $setupHash)
Write-Host ('   helper  : {0}' -f (Get-SizeText $helperSize))
if ($embedPayload) {
    Write-Host ('   payload : embedded from {0}' -f $PayloadDir)
    Write-Host  '             the setup executable is not byte-reproducible across build machines (ISCC'
    Write-Host  '             embeds the payload it finds in that directory), so publish the checksum you'
    Write-Host  '             measured here rather than a fixed one.'
} else {
    Write-Host ("   payload : downloaded from {0} at install time" -f $PayloadUrl)
    Write-Host  '             the release asset this URL points at is not byte-reproducible either: the zip'
    Write-Host  '             make_payload.ps1 writes depends on the deflate implementation of the machine'
    Write-Host  '             that ran it. Publish the checksum, and pass the same one here.'
}
