# Payload-snapshot provenance, shared by build.ps1 (which decides whether an existing
# snapshot may be reused) and make_payload.ps1 (which writes one).
#
# Dot-source this file; running it on its own does nothing.
#
# Why it exists: `installer/out/payload` is a *snapshot* - a copy of a build tree that was
# correct the day it was taken. build.ps1 used to keep whatever snapshot it found, so a plain
# release build could ship the game and the GPU plugin of an older tree (believed cause of the
# 2026-10-14 owner report that the installed game's Renderer offered D3D12 only: the snapshot
# kept by that build predated `REXGLUE_USE_VULKAN=ON` in the Windows presets). The manifest
# now records the snapshot's provenance - the graphics backends of the tree it came from and
# the commit it was taken at - and `Test-PayloadSnapshotCurrent` compares the snapshot against
# the tree it claims to be a copy of. A snapshot that cannot prove that is not reused by
# build.ps1's release path.
#
# Kept to Windows PowerShell 5.1: build.ps1 takes payload snapshots with powershell.exe.

# The payload files whose bytes come out of the build tree itself. The rest of the snapshot -
# the Visual C++ runtime DLLs and an explicitly selected external Vulkan loader -
# comes from outside the build tree. A staged source-built loader is checked by hash.
function Get-PayloadRuntimeFiles {
    param([string] $BuildDir = '')
    $inventory = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../config/runtime-modules.json') -Raw | ConvertFrom-Json
    $cache = @{}
    if ($BuildDir -and (Test-Path -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt'))) {
        foreach ($line in Get-Content -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt')) {
            if ($line -match '^([^/#:=][^:=]*):[^=]*=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
        }
    }
    $names = @($inventory.core) + @($inventory.conditional.windows)
    $options = @{
        tracy = @('REXGLUE_ENABLE_TRACY')
        vulkan = @('REXGLUE_USE_VULKAN', 'REXGLUE_GPU_VULKAN')
        d3d12 = @('REXGLUE_USE_D3D12')
    }
    foreach ($feature in @('tracy', 'vulkan', 'd3d12')) {
        $enabled = $true
        foreach ($option in $options[$feature]) {
            if ($cache.ContainsKey($option)) {
                $enabled = $cache[$option] -notmatch '^(|0|OFF|NO|FALSE|N|IGNORE|NOTFOUND)$|.*-NOTFOUND$'
                break
            }
        }
        if ($enabled) { $names += @($inventory.conditional.$feature) }
    }
    return @($names | ForEach-Object { $_ + '.dll' })
}


function Get-PayloadLicenseFiles {
    param([string] $BuildDir, [string] $RepoRoot)
    $licenseConfig = Join-Path $PSScriptRoot '../config/runtime-licenses.json'
    $inventory = Get-Content -LiteralPath $licenseConfig -Raw | ConvertFrom-Json
    $vulkanRoot = Join-Path $BuildDir '_deps/rex-vulkan'
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        foreach ($line in Get-Content -LiteralPath $cache) {
            if ($line -match '^REXGLUE_VULKAN_SOURCE_ROOT:[^=]*=(.*)$') { $vulkanRoot = $Matches[1] }
        }
    }
    $runtime = @(Get-PayloadRuntimeFiles -BuildDir $BuildDir)
    foreach ($entry in $inventory) {
        if ($entry.platforms -notcontains 'windows') { continue }
        $properties = @($entry.PSObject.Properties.Name)
        if ($properties -contains 'when') {
            if ($entry.when -eq 'vulkan' -and $runtime -notcontains 'rexgpu-xenos-vulkan.dll') { continue }
            if ($entry.when -eq 'tracy' -and $runtime -notcontains 'TracyClient.dll') { continue }
        }
        if ($properties -contains 'fromVulkanSource') { $source = Join-Path $vulkanRoot $entry.fromVulkanSource }
        else { $source = Join-Path $RepoRoot $entry.from }
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "missing runtime license: $source" }
        if ((Get-Item -LiteralPath $source).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "linked runtime license: $source"
        }
        [pscustomobject]@{ Name = ('licenses/' + $entry.to); Source = $source }
    }
}

function Get-PayloadTreeFiles {
    param([string] $BuildDir = '')
    return @('rb_blitz.exe', 'rb_blitz_load_monitor.exe',
             'graphics-preparation.json', 'collect_diagnostics.ps1', 'Collect Diagnostics.cmd') +
             @(Get-PayloadRuntimeFiles -BuildDir $BuildDir)
}

# Authored repository files are not in SourceDir but still determine the bytes
# of the installer snapshot. A message-only edit must invalidate an old payload.
function Get-PayloadRepoFiles {
    return @('assets/messages/feed.json')
}

# The graphics backends a build tree was configured with, in the order the Renderer row offers
# them. Read from CMakeCache.txt because that is the configuration the GPU plugin itself was
# built from; what the packaging half can know about a tree is what it was configured with.
function Get-PayloadBackends {
    param([Parameter(Mandatory = $true)][string] $BuildDir)

    $backends = @()
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache)) { return $backends }
    foreach ($line in @(Get-Content -LiteralPath $cache)) {
        if ($line -match '^REXGLUE_USE_D3D12:BOOL=ON') { $backends += 'd3d12' }
        if ($line -match '^REXGLUE_USE_VULKAN:BOOL=ON') { $backends += 'vulkan' }
    }
    return $backends
}

# A payload manifest, as far as the provenance checks read it: the [payload] table and the
# [[files]] entries. The file is generated by make_payload.ps1, so this is a line scanner
# rather than a TOML parser, and nothing outside this file depends on the rest of the format.
function Read-PayloadManifest {
    param([Parameter(Mandatory = $true)][string] $ManifestPath)

    $section = ''
    $backends = @()
    $commit = ''
    $loaderOrigin = ''
    # Plain arrays, appended to: an `@()` array subexpression over a New-Object generic list
    # throws "Argument types do not match" on PowerShell 7.4 here, and the lists are tiny.
    $entries = @()
    $entry = $null

    foreach ($raw in @(Get-Content -LiteralPath $ManifestPath)) {
        $line = $raw.Trim()
        if ($line.Length -eq 0 -or $line.StartsWith('#')) { continue }

        if ($line -eq '[payload]') { $section = 'payload'; continue }
        if ($line -eq '[game]') { $section = 'game'; continue }
        if ($line -eq '[[files]]') {
            if ($null -ne $entry) { $entries += $entry }
            $entry = [pscustomobject]@{ Path = ''; Size = [int64]0; Sha256 = '' }
            $section = 'files'
            continue
        }
        if ($line.StartsWith('[')) { $section = 'other'; continue }

        $equals = $line.IndexOf('=')
        if ($equals -lt 0) { continue }
        $key = $line.Substring(0, $equals).Trim()
        $value = $line.Substring($equals + 1).Trim().Trim('"')

        if ($section -eq 'payload') {
            if ($key -eq 'backends') {
                $backends = @($value.Split(',') |
                    ForEach-Object { $_.Trim() } |
                    Where-Object { $_.Length -gt 0 })
            } elseif ($key -eq 'vulkan_loader_origin') {
                $loaderOrigin = $value
            } elseif ($key -eq 'commit') {
                $commit = $value
            }
        } elseif ($section -eq 'files' -and $null -ne $entry) {
            if ($key -eq 'path') { $entry.Path = $value }
            elseif ($key -eq 'size') { $entry.Size = [int64]$value }
            elseif ($key -eq 'sha256') { $entry.Sha256 = $value.ToLowerInvariant() }
        }
    }
    if ($null -ne $entry) { $entries += $entry }

    return [pscustomobject]@{ Backends = @($backends); Commit = $commit; LoaderOrigin = $loaderOrigin; Entries = @($entries) }
}

# The reasons an existing snapshot may NOT stand in for the given build tree, as a list of
# sentences. An empty list means "this snapshot is this tree": the backends agree, the commit
# agrees when both name one, and every file the tree owns is the tree's copy byte for byte.
function Test-PayloadSnapshotCurrent {
    param(
        [Parameter(Mandatory = $true)][string] $PayloadDir,
        [Parameter(Mandatory = $true)][string] $SourceDir,
        [Parameter(Mandatory = $true)][string[]] $ExpectedBackends,
        [string] $ExpectedCommit = '',
        [string] $RepoRoot = ''
    )

    $reasons = @()
    $manifestPath = Join-Path $PayloadDir 'payload-manifest.toml'
    if (-not (Test-Path -LiteralPath $manifestPath)) {
        $reasons += "there is no payload-manifest.toml in $PayloadDir"
        return $reasons
    }

    $manifest = Read-PayloadManifest -ManifestPath $manifestPath

    # Provenance. Absent means the snapshot predates it, and then nothing about it can be
    # trusted to describe this tree - which is the state every snapshot before 2026-10-14 is in.
    if ($manifest.Backends.Count -eq 0 -and $manifest.Commit.Length -eq 0) {
        $reasons += 'the snapshot records no provenance (it predates the manifest backends/commit)'
    } else {
        if ($ExpectedBackends.Count -eq 0) {
            $reasons += "the build tree $SourceDir has no CMakeCache.txt, so its backends are unknown"
        } else {
            $recorded = @($manifest.Backends | Sort-Object)
            $expected = @($ExpectedBackends | Sort-Object)
            if (($recorded -join ',') -ne ($expected -join ',')) {
                $was = 'none'
                if ($manifest.Backends.Count -gt 0) { $was = $manifest.Backends -join ',' }
                $reasons += "the snapshot was built for $was and the build tree is configured for $($ExpectedBackends -join ',')"
            }
        }
        if ($manifest.Commit.Length -gt 0 -and $ExpectedCommit.Length -eq 0) {
            $reasons += 'the snapshot records a commit; workspace builds no longer record commits'
        } elseif ($ExpectedCommit.Length -gt 0 -and $manifest.Commit.Length -gt 0 -and
                  $manifest.Commit.ToLowerInvariant() -ne $ExpectedCommit.ToLowerInvariant()) {
            $reasons += "the snapshot says it is commit $($manifest.Commit) and this build is $ExpectedCommit"
        }
    }

    # A Vulkan build has to carry its loader. The SDK opens vulkan-1.dll at run time - it is
    # never an import - so nothing else in the snapshot would reveal its absence.
    if ($ExpectedBackends -contains 'vulkan') {
        $has_loader = $false
        foreach ($entry in $manifest.Entries) {
            if ($entry.Path -eq 'vulkan-1.dll') { $has_loader = $true }
        }
        if (-not $has_loader) {
            $reasons += 'the build tree offers Vulkan and the snapshot ships no vulkan-1.dll'
        }
        if ($manifest.LoaderOrigin -eq 'staged') {
            $loader = Join-Path $SourceDir 'vulkan-1.dll'
            $entry = @($manifest.Entries | Where-Object { $_.Path -eq 'vulkan-1.dll' })
            if (-not (Test-Path -LiteralPath $loader -PathType Leaf) -or $entry.Count -ne 1 -or
                (Get-FileHash -LiteralPath $loader -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry[0].Sha256) {
                $reasons += 'vulkan-1.dll is missing or differs from the source-built staged loader'
            }
        } elseif ($manifest.LoaderOrigin -notin @('override', 'external')) {
            $reasons += 'the snapshot does not identify its Vulkan loader source'
        }
    }

    # The files the tree owns, byte for byte.
    $tree_files = Get-PayloadTreeFiles -BuildDir $SourceDir
    foreach ($entry in $manifest.Entries) {
        if ($tree_files -notcontains $entry.Path) { continue }
        $source = Join-Path $SourceDir $entry.Path
        if (-not (Test-Path -LiteralPath $source)) {
            $reasons += "the build tree has no $($entry.Path)"
            continue
        }
        $item = Get-Item -LiteralPath $source
        if ([int64]$item.Length -ne $entry.Size) {
            $reasons += "$($entry.Path) is $($item.Length) bytes in the build tree and $($entry.Size) in the snapshot"
            continue
        }
        $digest = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($digest -ne $entry.Sha256) {
            $reasons += "$($entry.Path) is not the build tree's copy (the digests differ)"
        }
    }
    foreach ($name in $tree_files) {
        $listed = $false
        foreach ($entry in $manifest.Entries) {
            if ($entry.Path -eq $name) { $listed = $true }
        }
        if (-not $listed) { $reasons += "the snapshot does not list $name" }
    }

    if (-not $RepoRoot) { $RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }
    foreach ($license in (Get-PayloadLicenseFiles -BuildDir $SourceDir -RepoRoot $RepoRoot)) {
        $entry = @($manifest.Entries | Where-Object { $_.Path -eq $license.Name })
        if ($entry.Count -ne 1 -or
            (Get-FileHash -LiteralPath $license.Source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry[0].Sha256) {
            $reasons += "$($license.Name) is missing or differs from the configured runtime source"
        }
    }
    foreach ($name in (Get-PayloadRepoFiles)) {
        $source = Join-Path $RepoRoot $name
        $entry = @($manifest.Entries | Where-Object { $_.Path -eq $name })
        if ($entry.Count -ne 1) {
            $reasons += "the snapshot does not list $name exactly once"
            continue
        }
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            $reasons += "the repository has no $name"
            continue
        }
        $item = Get-Item -LiteralPath $source
        if ([int64]$item.Length -ne $entry[0].Size -or
            (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry[0].Sha256) {
            $reasons += "$name has changed since the payload snapshot was made"
        }
    }

    return $reasons
}
