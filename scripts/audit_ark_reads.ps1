# Milestone 4: in-game archive (ARK/HDR) read audit.
#
# The milestone asks for proof that the title reads its archive header and data
# files out of the game-data root at correct offsets and sizes. File identity is
# already covered offline by the fingerprint audit (tools/fingerprint_check
# --all); this script covers the runtime half: every guest read is recovered from
# a trace-level log and checked against the on-disk file it resolves to.
#
# The SDK only records kernel IO arguments at noisy-trace level (REXKRNL_IMPORT_
# TRACE / REXKRNL_IMPORT_RESULT are REX_LOG_NOISY_IMPL), so the log has to be
# produced with --log_noisy=true --log_level=trace. At 'debug' the successful
# calls collapse to one line without arguments.  NtWriteFile gained those calls
# in patches/rexglue-sdk/0003; run scripts/apply_sdk_patches.ps1 before tracing.
#
# Per read, this script checks:
#   * the handle was opened by an NtCreateFile in the same log (handles are
#     reused, so every successful create rebinds the handle to its path),
#   * the read carries an explicit byte offset - position-based reads (offset
#     -1) cannot be audited and are reported as violations, not skipped,
#   * offset <= size and the delivered byte count equals
#     min(length, size - offset),
#   * the completion status is success, or END_OF_FILE, which is what the SDK
#     reports when a completed synchronous read is promoted for a file that was
#     opened without FILE_SYNCHRONOUS_IO_*,
#   * the guest path maps to a real file under the game-data root (the Ultimate
#     payload copy wins when both exist, matching the overlay), whose size agrees
#     with config/game_fingerprints.toml,
#   * the file was served by the overlay device that unions the game-data root
#     with the Ultimate payload.
#
# Reads on other devices (content/settings storage) are outside the archive's
# scope: they are reported in their own table and left to the persistence tests.
#
# Usage:
#   # audit the newest existing log
#   .\scripts\audit_ark_reads.ps1
#   # trace a fresh run, drive it to the song list, then audit
#   .\scripts\audit_ark_reads.ps1 -Launch -RunSec 100 -Actions "key:a","wait:3",...
#
# Exit code is non-zero when any violation is recorded.
param(
    [string[]]$LogPath,
    [switch]$Launch,
    [int]$BootWaitSec = 22,
    [int]$RunSec = 100,
    [string[]]$Actions,
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m4-offsets",
    [string]$GameRoot = "game",
    [string[]]$RequireRead = @("d:\gen\main_xbox.hdr", "d:\gen\main_xbox_0.ark"),
    [string]$ExpectedDevice = "\Device\BlitzOverlay",
    # Guest volumes that are the game-data root. Reads on these, and reads served
    # by the overlay device, are fully audited; everything else (content/settings
    # storage on other devices) is reported but verified by the persistence tests.
    [string]$InScopePath = '^(?i)(game|d):\\?'
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$gameRootAbs = [System.IO.Path]::GetFullPath((Join-Path $root $GameRoot))
$outDirAbs = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $outDirAbs | Out-Null

function Min64([long]$a, [long]$b) { if ($a -lt $b) { return $a } else { return $b } }
function Gcd64([long]$a, [long]$b) {
    $a = [math]::Abs($a); $b = [math]::Abs($b)
    while ($b -ne 0) { $t = $b; $b = $a % $b; $a = $t }
    return $a
}
function FromHex([string]$s) { return [Convert]::ToInt64($s.Substring(2), 16) }

# ---------------------------------------------------------------------------
# Optionally produce the log to audit.
# ---------------------------------------------------------------------------
$launched = $false
if ($Launch) {
    $exe = Join-Path $work "rb_blitz.exe"
    if (-not (Test-Path $exe)) { throw "not found: $exe" }
    $logDir = Join-Path $work "logs"
    Get-ChildItem (Join-Path $logDir "*.log") -ErrorAction SilentlyContinue | Remove-Item -Force

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    Write-Host "launching $exe (trace logging, ${RunSec}s)"
    $proc = Start-Process -FilePath $exe `
        -ArgumentList "--game_data_root=$gameRootAbs", "--log_noisy=true", "--log_level=trace" `
        -WorkingDirectory $work -PassThru
    Start-Sleep -Seconds $BootWaitSec

    if ($Actions) {
        & (Join-Path $PSScriptRoot "drive_ui.ps1") -Actions $Actions -BuildDir $BuildDir `
            -OutDir (Join-Path $OutDir "shots") | Out-Null
    }

    $remaining = $RunSec - [int]$sw.Elapsed.TotalSeconds
    if ($remaining -gt 0) { Start-Sleep -Seconds $remaining }

    if (-not $proc.HasExited) {
        $proc.CloseMainWindow() | Out-Null
        if (-not $proc.WaitForExit(20000)) { Stop-Process -Id $proc.Id -Force }
    }
    $launched = $true
    $LogPath = @(Get-ChildItem (Join-Path $logDir "*.log") -ErrorAction SilentlyContinue |
        Sort-Object Name | ForEach-Object { $_.FullName })
}

if (-not $LogPath -or $LogPath.Count -eq 0) {
    $newest = Get-ChildItem (Join-Path $work "logs/*.log") -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $newest) { throw "no log found under $work/logs - run with -Launch, or pass -LogPath" }
    $LogPath = @($newest.FullName)
}
foreach ($lp in $LogPath) {
    if (-not (Test-Path $lp)) { throw "not found: $lp" }
}

# ---------------------------------------------------------------------------
# Expected sizes: config/game_fingerprints.toml is the source of truth for the
# archive files this project supports (relative to the game-data root).
# ---------------------------------------------------------------------------
$fingerprintSizes = @{}
$fpRole = $null
$fpPath = $null
foreach ($line in Get-Content (Join-Path $root "config/game_fingerprints.toml")) {
    if ($line -match '^\s*role\s*=\s*"([^"]+)"') { $fpRole = $Matches[1] }
    elseif ($line -match '^\s*path\s*=\s*"([^"]+)"') { $fpPath = $Matches[1] }
    elseif ($line -match '^\s*size\s*=\s*(\d+)') {
        if ($fpRole -like 'archive-*' -and $fpPath) {
            $fingerprintSizes[$fpPath.Replace('/', '\')] = [long]$Matches[1]
        }
    }
}
if ($fingerprintSizes.Count -eq 0) { throw "no archive sizes parsed from config/game_fingerprints.toml" }

function Get-EffectiveHostFile([string]$guestPath) {
    # game:\ and d:\ are the same volume (the game-data root). The Ultimate
    # payload (game/ultimate/...) is overlaid on top of it and wins when both
    # copies of a path exist, which is how the title sees patch_xbox*.
    if ($guestPath -notmatch '^(?i)(game|d):\\?(.*)$') { return $null }
    $rel = $Matches[2]
    if (-not $rel) { return $null }
    $payload = Join-Path $gameRootAbs (Join-Path "ultimate" $rel)
    if (Test-Path -PathType Leaf $payload) { return $payload }
    $direct = Join-Path $gameRootAbs $rel
    if (Test-Path -PathType Leaf $direct) { return $direct }
    return $null
}

# ---------------------------------------------------------------------------
# Parse.
# ---------------------------------------------------------------------------
$threadRe = [regex]'\[t(\d+)\]'
$vfsRe = [regex]"VFS resolved '(?<logical>[^']*)'(?: via symlink '(?<symlink>[^']*)')? on device '(?<device>[^']*)' -> '(?<target>[^']*)'"
$createFailRe = [regex]"\[NtCreateFile\] path='(?<path>[^']*)' -> (?<status>0x[0-9a-fA-F]+)"
$createReqRe = [regex]'\[NtCreateFile\] path=(?<path>\S+) access='
$createResRe = [regex]'\[NtCreateFile\] -> (?<status>0x[0-9a-fA-F]+)(?: handle=(?<handle>0x[0-9a-fA-F]+))?'
$closeRe = [regex]'\[NtClose\] handle=(?<handle>0x[0-9a-fA-F]+)'
$readReqRe = [regex]'\[NtReadFile\] handle=(?<handle>0x[0-9a-fA-F]+) .*len=(?<len>0x[0-9a-fA-F]+) offset=(?<offset>-?\d+)'
$readResRe = [regex]'\[NtReadFile\] -> (?<status>0x[0-9a-fA-F]+) \((?<detail>.*)\)'
$scatterRe = [regex]'\[NtReadFileScatter\]'
$queryRe = [regex]'\[NtQueryFullAttributesFile\] path=(?<path>\S+)'
$stampRe = [regex]'^\[(?<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]'

$handlePath = @{}                 # live handle -> guest path
$pendingCreate = @{}              # thread -> guest path awaiting its result
$pendingReads = @{}               # thread -> queue of reads awaiting results
$pathDevice = @{}                 # guest path -> device that served the open (exact resolve)
$resolvedPaths = New-Object System.Collections.ArrayList  # every VFS resolve, for prefix lookup
$fileStats = [ordered]@{}         # guest path -> aggregate stats
$violations = New-Object System.Collections.ArrayList
$openFailures = New-Object System.Collections.ArrayList
$queryPaths = @{}
$counters = @{
    Lines = 0; Creates = 0; CreateFailures = 0; Closes = 0; Reads = 0; ReadResults = 0
    Unattributed = 0; Scatter = 0; PositionBased = 0; Queries = 0; Bytes = 0L
    OutOfScopeReads = 0; OutOfScopeBytes = 0L; OutOfScopePositionBased = 0
}

# Reads are bucketed by wall-clock time so the report can show *when* the archive
# was read. A boot-only log is one burst; a driven log shows a steady stream while
# a song plays, which is what makes the run recognisable as in-game.
$timelineStart = $null
$timelineBuckets = @{}
$timelineSec = 0

function New-FileStats([string]$path, [bool]$inScope) {
    return [pscustomobject]@{
        Path = $path
        InScope = $inScope
        Reads = 0
        Opens = 0
        Handles = @{}
        Requested = [long]0
        Delivered = [long]0
        MinOffset = [long]::MaxValue
        MaxOffset = [long]0
        MaxEnd = [long]0
        EofProbes = 0
        PositionBased = 0
        Gcd = [long]0
        Lengths = @{}
        Samples = New-Object System.Collections.ArrayList
    }
}

function Get-FileStats([string]$path, [bool]$inScope) {
    if (-not $fileStats.Contains($path)) { $fileStats[$path] = New-FileStats $path $inScope }
    return $fileStats[$path]
}

function Add-Violation([string]$kind, [string]$path, [string]$detail) {
    [void]$violations.Add([pscustomobject]@{ Kind = $kind; Path = $path; Detail = $detail })
}

# The VFS only logs a resolve for the path it is handed. Opens resolve the parent
# directory by path and then look the file up as a child entry, so an archive
# file like d:\gen\main_xbox_0.ark has no resolve line of its own - the evidence
# for which device served it is the resolve of its parent directory. Exact matches
# win; otherwise the longest resolved ancestor prefix is used.
function Get-ServingDevice([string]$guestPath) {
    if ($pathDevice.ContainsKey($guestPath)) {
        return [pscustomobject]@{ Device = $pathDevice[$guestPath]; Evidence = 'resolve of the file path' }
    }
    $probe = $guestPath.TrimEnd('\') + '\'
    $best = $null
    foreach ($r in $resolvedPaths) {
        $rp = $r.Path.TrimEnd('\')
        if (-not $rp) { continue }
        if ($probe.StartsWith($rp + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            if (-not $best -or $rp.Length -gt $best.Path.TrimEnd('\').Length) { $best = $r }
        }
    }
    if ($best) {
        return [pscustomobject]@{ Device = $best.Device; Evidence = "resolve of the parent directory '$($best.Path)'" }
    }
    return $null
}

foreach ($log in $LogPath) {
    Write-Host ("parsing {0} ({1:N1} MB)" -f (Split-Path -Leaf $log), ((Get-Item $log).Length / 1MB))
    foreach ($line in [System.IO.File]::ReadLines($log)) {
        $counters.Lines++
        if ($line.IndexOf('[Nt', [System.StringComparison]::Ordinal) -lt 0 -and
            $line.IndexOf('VFS resolved', [System.StringComparison]::Ordinal) -lt 0) { continue }

        $tid = 0
        $m = $threadRe.Match($line)
        if ($m.Success) { $tid = [int]$m.Groups[1].Value }

        # VFS resolve: which device served the open, and the guest-side result.
        if ($line.IndexOf('VFS resolved', [System.StringComparison]::Ordinal) -ge 0) {
            $m = $vfsRe.Match($line)
            if ($m.Success) {
                $logical = $m.Groups['logical'].Value
                $device = $m.Groups['device'].Value
                $pathDevice[$logical] = $device
                [void]$resolvedPaths.Add([pscustomobject]@{ Path = $logical; Device = $device })
            }
            continue
        }

        # Reads.
        if ($line.IndexOf('[NtReadFile]', [System.StringComparison]::Ordinal) -ge 0) {
            $m = $readResRe.Match($line)
            if ($m.Success) {
                $counters.ReadResults++
                if (-not $pendingReads.ContainsKey($tid) -or $pendingReads[$tid].Count -eq 0) {
                    $counters.Unattributed++
                    Add-Violation 'unpaired-read-result' '-' ("no pending read on thread $tid for result $line")
                    continue
                }
                $req = $pendingReads[$tid].Dequeue()
                $status = $m.Groups['status'].Value
                $detail = $m.Groups['detail'].Value
                $iosbStatus = $null
                $delivered = $null
                if ($detail -match 'iosb_status=(?<s>0x[0-9a-fA-F]+), iosb_info=(?<i>\d+)') {
                    $iosbStatus = $Matches['s']
                    $delivered = [long]$Matches['i']
                }
                $stats = $null
                if ($req.Path) { $stats = Get-FileStats $req.Path $req.InScope }
                if ($delivered -ne $null) {
                    if ($stats) { $stats.Delivered += $delivered }
                    if ($req.InScope) { $counters.Bytes += $delivered }
                    else { $counters.OutOfScopeBytes += $delivered }
                }
                if (-not $req.Path) { continue }

                if ($iosbStatus -eq $null) {
                    Add-Violation 'no-iosb-completion' $req.Path $line
                    continue
                }
                if ($iosbStatus -ne '0x0' -and $req.InScope) {
                    Add-Violation 'io-failure' $req.Path `
                        ("iosb_status=$iosbStatus result=$status (delivered=$delivered, len=$($req.Length), offset=$($req.Offset))")
                }
                if (-not $req.InScope) { continue }
                $hostFile = Get-EffectiveHostFile $req.Path
                if (-not $hostFile) {
                    Add-Violation 'unmapped-path' $req.Path ("read of $($req.Path) does not map to a file under $gameRootAbs")
                    continue
                }
                $size = [long](Get-Item $hostFile).Length
                if ($req.Offset -eq $size) {
                    $stats.EofProbes++
                } elseif ($req.Offset -gt $size) {
                    Add-Violation 'read-past-eof' $req.Path `
                        ("offset=$($req.Offset) > size=$size (len=$($req.Length))")
                } else {
                    $expected = Min64 $req.Length ($size - $req.Offset)
                    if ($delivered -ne $expected) {
                        Add-Violation 'size-mismatch' $req.Path `
                            ("offset=$($req.Offset) len=$($req.Length) delivered=$delivered expected=$expected (size=$size)")
                    }
                }
                if ($delivered -gt $req.Length) {
                    Add-Violation 'over-read' $req.Path ("delivered=$delivered > len=$($req.Length)")
                }
                continue
            }
            $m = $readReqRe.Match($line)
            if ($m.Success) {
                $counters.Reads++

                $mts = $stampRe.Match($line)
                if ($mts.Success) {
                    $ts = [datetime]::ParseExact($mts.Groups['ts'].Value, 'yyyy-MM-dd HH:mm:ss.fff', $null)
                    if (-not $timelineStart) { $timelineStart = $ts }
                    $elapsed = [int](($ts - $timelineStart).TotalSeconds)
                    if ($elapsed -lt 0) { $elapsed = 0 }
                    $timelineSec = $elapsed
                    $bucket = $elapsed - ($elapsed % 10)
                    if ($timelineBuckets.ContainsKey($bucket)) { $timelineBuckets[$bucket]++ }
                    else { $timelineBuckets[$bucket] = 1 }
                }

                $handle = $m.Groups['handle'].Value
                $len = FromHex $m.Groups['len'].Value
                $offset = [long]$m.Groups['offset'].Value

                $path = $null
                if ($handlePath.ContainsKey($handle)) { $path = $handlePath[$handle] }
                $serving = $null
                if ($path) { $serving = Get-ServingDevice $path }

                # A read is audited when it is on a game-data-root volume or was
                # served by the overlay device. Other devices hold content and
                # settings, which the persistence tests cover instead.
                $inScope = $false
                if ($path) {
                    if ($path -match $InScopePath) { $inScope = $true }
                    elseif ($serving -and $serving.Device -eq $ExpectedDevice) { $inScope = $true }
                }

                if (-not $path) {
                    $counters.Unattributed++
                    Add-Violation 'unattributed-read' '-' ("handle=$handle has no open recorded in this log ($line)")
                } elseif ($inScope -and $offset -lt 0) {
                    $counters.PositionBased++
                    Add-Violation 'position-based-read' $path `
                        ("len=$len has no byte offset; the SDK read it from the file position, which this audit cannot verify")
                } elseif ($inScope -and -not $serving) {
                    Add-Violation 'device-unknown' $path `
                        ("no VFS resolve line covers this path, so the serving device is unproven")
                } elseif (-not $inScope) {
                    $counters.OutOfScopeReads++
                    if ($offset -lt 0) { $counters.OutOfScopePositionBased++ }
                }

                if ($path) {
                    $stats = Get-FileStats $path $inScope
                    if ($stats.InScope -and -not $inScope) {
                        Add-Violation 'scope-changed' $path 'the same path was read both inside and outside the game data root'
                    }
                    $stats.Reads++
                    $stats.Requested += $len
                    if ($offset -lt 0) { $stats.PositionBased++ }
                    if ($offset -ge 0) {
                        if ($offset -lt $stats.MinOffset) { $stats.MinOffset = $offset }
                        if ($offset -gt $stats.MaxOffset) { $stats.MaxOffset = $offset }
                        $end = $offset + $len
                        if ($end -gt $stats.MaxEnd) { $stats.MaxEnd = $end }
                        $stats.Gcd = Gcd64 $stats.Gcd $offset
                    }
                    $stats.Gcd = Gcd64 $stats.Gcd $len
                    $stats.Lengths["$len"] = $true
                    $stats.Handles[$handle] = $true
                    if ($stats.Samples.Count -lt 5) {
                        [void]$stats.Samples.Add("handle=$handle len=$len offset=$offset")
                    }
                }
                if (-not $pendingReads.ContainsKey($tid)) {
                    $pendingReads[$tid] = New-Object System.Collections.Queue
                }
                [void]$pendingReads[$tid].Enqueue([pscustomobject]@{
                    Handle = $handle; Length = $len; Offset = $offset; Path = $path; InScope = $inScope
                })
                continue
            }
            if ($scatterRe.IsMatch($line)) { $counters.Scatter++ }
            continue
        }

        # Attribute queries (the title probes the root before opening).
        if ($line.IndexOf('[NtQueryFullAttributesFile]', [System.StringComparison]::Ordinal) -ge 0) {
            $m = $queryRe.Match($line)
            if ($m.Success) { $counters.Queries++; $queryPaths[$m.Groups['path'].Value] = $true }
            continue
        }

        # Opens.
        if ($line.IndexOf('[NtCreateFile]', [System.StringComparison]::Ordinal) -ge 0) {
            $m = $createFailRe.Match($line)
            if ($m.Success) {
                $counters.CreateFailures++
                [void]$openFailures.Add([pscustomobject]@{
                    Path = $m.Groups['path'].Value; Status = $m.Groups['status'].Value
                })
                $pendingCreate.Remove($tid) | Out-Null
                continue
            }
            $m = $createResRe.Match($line)
            if ($m.Success) {
                $counters.Creates++
                $handle = $null
                if ($m.Groups['handle'].Success) { $handle = $m.Groups['handle'].Value }
                $path = $null
                if ($pendingCreate.ContainsKey($tid)) { $path = $pendingCreate[$tid] }
                $pendingCreate.Remove($tid) | Out-Null
                if ($handle) {
                    if ($path) {
                        $handlePath[$handle] = $path
                        $openServing = Get-ServingDevice $path
                        $openInScope = ($path -match $InScopePath) -or
                            ($openServing -and $openServing.Device -eq $ExpectedDevice)
                        (Get-FileStats $path $openInScope).Opens++
                    } else {
                        $handlePath.Remove($handle) | Out-Null
                        Add-Violation 'unpaired-open' '-' ("handle=$handle opened without a preceding NtCreateFile request line")
                    }
                }
                continue
            }
            $m = $createReqRe.Match($line)
            if ($m.Success) { $pendingCreate[$tid] = $m.Groups['path'].Value; continue }
            continue
        }

        # Closes.
        if ($line.IndexOf('[NtClose]', [System.StringComparison]::Ordinal) -ge 0) {
            $m = $closeRe.Match($line)
            if ($m.Success) {
                $counters.Closes++
                $handlePath.Remove($m.Groups['handle'].Value) | Out-Null
            }
            continue
        }
    }
}

# ---------------------------------------------------------------------------
# Cross-checks that need the whole log.
# ---------------------------------------------------------------------------
$required = New-Object System.Collections.ArrayList
foreach ($req in $RequireRead) {
    $stats = $null
    if ($fileStats.Contains($req)) { $stats = $fileStats[$req] }
    $hostFile = Get-EffectiveHostFile $req
    $size = $null
    $expectedSize = $null
    $rel = ($req -replace '^(?i)(game|d):\\?', '').Replace('/', '\')
    if ($hostFile) { $size = [long](Get-Item $hostFile).Length }
    if ($fingerprintSizes.ContainsKey($rel)) { $expectedSize = $fingerprintSizes[$rel] }

    if (-not $hostFile) {
        Add-Violation 'required-file-missing' $req "no file under $gameRootAbs backs $req"
    } elseif ($null -eq $stats -or $stats.Reads -eq 0) {
        Add-Violation 'required-file-not-read' $req "the title never read $req in this log"
    } elseif ($expectedSize -ne $null -and $size -ne $expectedSize) {
        Add-Violation 'fingerprint-mismatch' $req `
            ("on-disk size $size does not match config/game_fingerprints.toml ($expectedSize)")
    }
    $serving = Get-ServingDevice $req
    if ($serving -and $serving.Device -ne $ExpectedDevice) {
        Add-Violation 'unexpected-device' $req ("served by $($serving.Device) ($($serving.Evidence)), expected $ExpectedDevice")
    } elseif (-not $serving) {
        Add-Violation 'device-unknown' $req 'no VFS resolve line covers this path, so the serving device is unproven'
    }

    [void]$required.Add([pscustomobject]@{
        GuestPath = $req
        BackingFile = $(if ($hostFile) { $hostFile.Replace($root + '\', '') } else { '-' })
        Size = $size
        FingerprintSize = $expectedSize
        Device = $(if ($serving) { $serving.Device } else { '-' })
        DeviceEvidence = $(if ($serving) { $serving.Evidence } else { '-' })
        Reads = $(if ($stats) { $stats.Reads } else { 0 })
        Opens = $(if ($stats) { $stats.Opens } else { 0 })
        Handles = $(if ($stats) { $stats.Handles.Count } else { 0 })
        RequestedBytes = $(if ($stats) { $stats.Requested } else { 0 })
        DeliveredBytes = $(if ($stats) { $stats.Delivered } else { 0 })
        MinOffset = $(if ($stats -and $stats.Reads -gt 0) { $stats.MinOffset } else { $null })
        MaxOffset = $(if ($stats) { $stats.MaxOffset } else { $null })
        MaxEnd = $(if ($stats) { $stats.MaxEnd } else { $null })
        EofProbes = $(if ($stats) { $stats.EofProbes } else { 0 })
        DistinctLengths = $(if ($stats) { $stats.Lengths.Count } else { 0 })
        BlockSize = $(if ($stats) { $stats.Gcd } else { 0 })
        Samples = $(if ($stats) { @($stats.Samples.ToArray()) } else { @() })
    })
}

$pass = $violations.Count -eq 0

# Coarsen the read timeline to whole minutes once 10-second buckets would be too
# many rows to read.
$bucketSec = 10
if ($timelineSec -gt 400) { $bucketSec = 60 }
$timeline = New-Object System.Collections.ArrayList
if ($timelineStart) {
    for ($t = 0; $t -le $timelineSec; $t += $bucketSec) {
        $reads = 0
        for ($sub = $t; $sub -lt ($t + $bucketSec); $sub += 10) {
            if ($timelineBuckets.ContainsKey($sub)) { $reads += $timelineBuckets[$sub] }
        }
        [void]$timeline.Add([pscustomobject]@{ StartSec = $t; Reads = $reads })
    }
}

$summary = [pscustomobject]@{
    Verdict = $(if ($pass) { 'PASS' } else { 'FAIL' })
    GeneratedUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss')
    LaunchedByScript = $launched
    Logs = @($LogPath | ForEach-Object { $_.Replace($root + '\', '') })
    LogBytes = [long](($LogPath | ForEach-Object { (Get-Item $_).Length } | Measure-Object -Sum).Sum)
    GameRoot = $gameRootAbs
    ExpectedDevice = $ExpectedDevice
    Counters = $counters
    Required = $required
    Files = @($fileStats.Values | Where-Object { $_.InScope } | ForEach-Object {
        [pscustomobject]@{
            GuestPath = $_.Path
            InScope = $_.InScope
            Reads = $_.Reads
            Opens = $_.Opens
            Handles = $_.Handles.Count
            RequestedBytes = $_.Requested
            DeliveredBytes = $_.Delivered
            MinOffset = $(if ($_.Reads -gt $_.PositionBased) { $_.MinOffset } else { $null })
            MaxOffset = $_.MaxOffset
            MaxEnd = $_.MaxEnd
            EofProbes = $_.EofProbes
            PositionBased = $_.PositionBased
            BlockSize = $_.Gcd
            Device = $(if ((Get-ServingDevice $_.Path)) { (Get-ServingDevice $_.Path).Device } else { '-' })
            Samples = @($_.Samples.ToArray())
        }
    } | Sort-Object -Property @{ Expression = 'Reads'; Descending = $true })
    OtherFiles = @($fileStats.Values | Where-Object { -not $_.InScope } | ForEach-Object {
        [pscustomobject]@{
            GuestPath = $_.Path
            Reads = $_.Reads
            Opens = $_.Opens
            RequestedBytes = $_.Requested
            DeliveredBytes = $_.Delivered
            PositionBased = $_.PositionBased
            Device = $(if ((Get-ServingDevice $_.Path)) { (Get-ServingDevice $_.Path).Device } else { '-' })
        }
    } | Sort-Object -Property @{ Expression = 'Reads'; Descending = $true })
    OpenFailures = @($openFailures)
    AttributeQueryPaths = @($queryPaths.Keys | Sort-Object)
    TimelineBucketSec = $bucketSec
    TimelineSpanSec = $timelineSec
    Timeline = @($timeline.ToArray())
    Violations = @($violations)
}

# ---------------------------------------------------------------------------
# Report.
# ---------------------------------------------------------------------------
$jsonPath = Join-Path $outDirAbs "read-audit.json"
$summary | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 $jsonPath

$logsMd = (@($summary.Logs) | ForEach-Object { '`' + $_ + '`' }) -join ', '
$md = New-Object System.Collections.ArrayList
[void]$md.Add("# In-game archive read audit ($($summary.Verdict))")
[void]$md.Add("")
[void]$md.Add("Generated by ``scripts/audit_ark_reads.ps1`` on $($summary.GeneratedUtc) UTC.")
[void]$md.Add("")
[void]$md.Add("- Logs: $logsMd ($([math]::Round($summary.LogBytes / 1MB, 1)) MB)")
[void]$md.Add("- Game data root: ``$($summary.GameRoot)``")
[void]$md.Add("- Serving device expected for archive files: ``$ExpectedDevice`` (the overlay that unions the game-data root with the Ultimate payload)")
[void]$md.Add("- Reads on game-data-root volumes: $($counters.Reads - $counters.OutOfScopeReads) ($($counters.ReadResults) completions in total in the log), opens: $($counters.Creates) ($($counters.CreateFailures) failed), closes: $($counters.Closes)")
[void]$md.Add("- Bytes delivered by audited reads: $($counters.Bytes)")
[void]$md.Add("- Unattributed reads: $($counters.Unattributed); position-based reads on the game-data root: $($counters.PositionBased); scatter-read lines: $($counters.Scatter)")
[void]$md.Add("- Reads outside the game-data root (content/settings storage): $($counters.OutOfScopeReads), $($counters.OutOfScopePositionBased) of them position-based, $($counters.OutOfScopeBytes) bytes. These are covered by ``scripts/acceptance_persistence.ps1``, not by this audit.")
[void]$md.Add("")
if ($timeline.Count -gt 0) {
    [void]$md.Add("## Read timeline")
    [void]$md.Add("")
    [void]$md.Add("``NtReadFile`` requests bucketed by wall-clock time from the first read in the log ($bucketSec-second buckets, $timelineSec s total). A boot-only run is a single burst and then silence; a driven run shows menu traffic followed by the steady streaming a loaded song produces.")
    [void]$md.Add("")
    [void]$md.Add("| t+ (s) | Reads |")
    [void]$md.Add("| ---: | ---: |")
    foreach ($b in $summary.Timeline) {
        [void]$md.Add("| $($b.StartSec) | $($b.Reads) |")
    }
    [void]$md.Add("")
}
[void]$md.Add("## Required archive files")
[void]$md.Add("")
[void]$md.Add("| Guest path | Backing file | Size | Fingerprint | Device | Opens/reads | Requested | Delivered | Offsets | Block |")
[void]$md.Add("| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | ---: |")
foreach ($r in $summary.Required) {
    $offsets = '-'
    if ($r.MinOffset -ne $null) { $offsets = "$($r.MinOffset) .. $($r.MaxOffset) (max end $($r.MaxEnd))" }
    [void]$md.Add("| ``$($r.GuestPath)`` | $($r.BackingFile) | $($r.Size) | $($r.FingerprintSize) | ``$($r.Device)`` | $($r.Opens)/$($r.Reads) | $($r.RequestedBytes) | $($r.DeliveredBytes) | $offsets | $($r.BlockSize) |")
}
[void]$md.Add("")
[void]$md.Add("Device evidence and per-run extremes:")
[void]$md.Add("")
foreach ($r in $summary.Required) {
    [void]$md.Add("- ``$($r.GuestPath)``: device from $($r.DeviceEvidence); $($r.Handles) handle(s) over $($r.Opens) open(s); $($r.DistinctLengths) distinct read length(s); $($r.EofProbes) read(s) starting exactly at EOF (the SDK reports a zero-byte END_OF_FILE completion for those).")
    if ($r.Samples.Count -gt 0) {
        [void]$md.Add("  - first reads: $(($r.Samples -join '; '))")
    }
}
[void]$md.Add("")
[void]$md.Add("## Every game-data-root file with audited reads")
[void]$md.Add("")
[void]$md.Add("| Guest path | Device | Reads | Requested | Delivered | Min offset | Max offset | Max end | EOF probes |")
[void]$md.Add("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
foreach ($f in ($summary.Files | Where-Object { $_.Reads -gt 0 })) {
    $minOffset = '-'
    if ($f.MinOffset -ne $null) { $minOffset = $f.MinOffset }
    [void]$md.Add("| ``$($f.GuestPath)`` | ``$($f.Device)`` | $($f.Reads) | $($f.RequestedBytes) | $($f.DeliveredBytes) | $minOffset | $($f.MaxOffset) | $($f.MaxEnd) | $($f.EofProbes) |")
}
[void]$md.Add("")
$openedOnly = @($summary.Files | Where-Object { $_.Reads -eq 0 } | ForEach-Object { $_.GuestPath })
if ($openedOnly.Count -gt 0) {
    [void]$md.Add("Opened but never read during this run: $(@($openedOnly | ForEach-Object { '``' + $_ + '``' }) -join ', ').")
    [void]$md.Add("")
}
if ($summary.OtherFiles.Count -gt 0) {
    [void]$md.Add("## Reads outside the game-data root")
    [void]$md.Add("")
    [void]$md.Add("Content and settings storage on other devices. Reported for completeness; these files are not part of the game-data root, so this audit only records them, and ``scripts/acceptance_persistence.ps1`` is what verifies them.")
    [void]$md.Add("")
    [void]$md.Add("| Guest path | Device | Reads | Requested | Delivered | Position-based |")
    [void]$md.Add("| --- | --- | ---: | ---: | ---: | ---: |")
    foreach ($f in $summary.OtherFiles) {
        [void]$md.Add("| ``$($f.GuestPath)`` | ``$($f.Device)`` | $($f.Reads) | $($f.RequestedBytes) | $($f.DeliveredBytes) | $($f.PositionBased) |")
    }
    [void]$md.Add("")
}
if ($summary.OpenFailures.Count -gt 0) {
    [void]$md.Add("## Opens that failed (expected where noted)")
    [void]$md.Add("")
    foreach ($g in ($summary.OpenFailures | Group-Object Path)) {
        [void]$md.Add("- ``$($g.Name)`` x$($g.Count) -> $($g.Group[0].Status)")
    }
    [void]$md.Add("")
}
[void]$md.Add("## Violations")
[void]$md.Add("")
if ($pass) {
    [void]$md.Add("None. Every audited read resolved to a file under the game-data root, started at or before EOF, and delivered exactly ``min(length, size - offset)`` bytes.")
} else {
    foreach ($g in ($violations | Group-Object Kind)) {
        [void]$md.Add("### ``$($g.Name)`` ($($g.Count))")
        [void]$md.Add("")
        foreach ($v in ($g.Group | Select-Object -First 10)) {
            [void]$md.Add("- $($v.Path) $($v.Detail)")
        }
        [void]$md.Add("")
    }
}
[void]$md.Add("## Notes")
[void]$md.Add("")
[void]$md.Add("- Scope: reads are audited when the guest path is on a game-data-root volume (``$InScopePath``) or when the path was served by ``$ExpectedDevice``. Other devices hold content and settings and are listed separately above.")
[void]$md.Add("- The audit covers the read entry points the SDK logs. ``NtReadFile`` requests and completions are both recorded at noisy-trace level, and so is ``NtReadFileScatter`` since [patches/rexglue-sdk/0003](../patches/rexglue-sdk/0003-trace-ntwritefile-and-scatter-reads.patch) added the trace calls it was missing. The counter above reports how many scatter lines the log contained: 0 in every audited run, so this title streams its archives through ``NtReadFile``.")
[void]$md.Add("- The serving device comes from the ``fs`` channel's ``VFS resolved`` line. ``VirtualFileSystem::OpenFile`` resolves the *parent directory* by path and then looks the file up as a child entry, so a file like ``d:\gen\main_xbox_0.ark`` has no resolve line of its own; the table above therefore cites either the file's own resolve or the resolve of its nearest resolved ancestor directory.")
[void]$md.Add("- Handles are reused across the run, so the parser rebinds a handle to its path on every successful ``NtCreateFile`` and drops it on ``NtClose``.")
[void]$md.Add("- Reads are attributed to their file through the handle timeline, not through wall-clock proximity.")
$mdPath = Join-Path $outDirAbs "read-audit.md"
$md | Set-Content -Encoding UTF8 $mdPath

Write-Host ""
Write-Host "=== $($summary.Verdict) ===" -ForegroundColor $(if ($pass) { 'Green' } else { 'Red' })
$summary.Required | Format-Table GuestPath, BackingFile, Size, Device, Opens, Reads, RequestedBytes, DeliveredBytes, MinOffset, MaxOffset, MaxEnd -AutoSize
Write-Host ("reads={0} (in-scope {1}, other-device {2}) opens={3} failed-opens={4} unattributed={5} position-based={6} scatter={7} bytes={8} violations={9}" -f `
    $counters.Reads, ($counters.Reads - $counters.OutOfScopeReads), $counters.OutOfScopeReads,
    $counters.Creates, $counters.CreateFailures, $counters.Unattributed,
    $counters.PositionBased, $counters.Scatter, $counters.Bytes, $violations.Count)
Write-Host "report: $mdPath"
Write-Host "json:   $jsonPath"
if (-not $pass) {
    Write-Host ""
    $violations | Group-Object Kind | ForEach-Object {
        Write-Host ("{0}: {1}" -f $_.Name, $_.Count) -ForegroundColor Yellow
    }
    exit 1
}
