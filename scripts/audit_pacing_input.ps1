# Milestone 5: verdict on frame pacing and input polling from a measurement run.
#
# Consumes the directory scripts/measure_pacing_input.ps1 produced
# (<dir>\run.json, <dir>\injections.json, <dir>\logs\logNN-*.log) and turns the
# SDK's traces into measured numbers with explicit thresholds:
#
#   * frames  - [VdSwap] (guest frame submitted) and [XE_SWAP] (frame presented)
#               rates, dt distribution from the host-microsecond deltas, hitch
#               counts, and counter continuity (a dropped log line would show up
#               as a gap and is reported as the measurement's own integrity);
#   * polling - [XamInputGetState] rate and worst gap for user 0, measured in
#               guest ticks (sub-millisecond resolution) and cross-checked
#               against the log's millisecond timestamps;
#   * presses - for every injected key-down/key-up pair, whether any guest poll
#               in the window saw the button bit, and how long after key-down the
#               first of those polls landed. That is what "a press is visible"
#               means concretely, and it is the reason the probes exist.
#
# Usage:
#   .\scripts\audit_pacing_input.ps1 -Dir out/m5-pacing/vsync-on
#   .\scripts\audit_pacing_input.ps1 -Dir out/m5-pacing/vsync-off -Tag vsync-off
#   .\scripts\audit_pacing_input.ps1 -SelfTest
#
# Writes pacing-input.json and pacing-input.md into -OutDir (default: -Dir) and
# exits 0 only when every check passes.
param(
    [string]$Dir,
    [string]$Tag,
    [string]$OutDir,
    [string]$InjectionsPath,
    # Synthesize a healthy and a broken measurement and assert the verdicts.
    [switch]$SelfTest,
    # --- thresholds ---------------------------------------------------------
    # A window shorter than this cannot back a "stable for a complete run"
    # claim, and it also guards against measuring a song-list preview.
    [double]$MinWindowSec = 60,
    # At 60 Hz a frame is 16.67 ms; anything slower than 20 ms on median is not
    # a 60 Hz-paced guest frame loop.
    [double]$MaxMedianDtMs = 20.0,
    # 40 ms is 1.5 vblank intervals at 60 Hz: a frame loop that misses a whole
    # interval for more than 1% of frames is not stable.
    [double]$MaxP99DtMs = 40.0,
    [double]$MaxHitchPct = 1.0,
    [double]$HitchFactor = 2.5,
    # Presses are held for at least 90 ms by the scripted route, so a poll gap
    # under 50 ms is the bound that matters; 250 ms is the "never stalled" bound.
    [double]$MaxPollGapP99Ms = 50.0,
    [double]$MaxPollGapMs = 250.0,
    [double]$MinPollRate = 30.0,
    # A press has to be held at least this long to be expected to survive a
    # poll; shorter ones are reported as characterization, not asserted.
    [double]$AssertHoldMs = 25.0,
    [double]$MaxProbeLatencyMs = 100.0,
    # Minimum number of in-song probes that make the press measurements usable.
    [int]$MinProbes = 8
)

$ErrorActionPreference = "Stop"

$vdRe = [regex]::new('\[VdSwap\] n=(\d+) dt_us=(\d+) guest_tick=(\d+)', 'Compiled')
$xeRe = [regex]::new('\[XE_SWAP\] n=(\d+) dt_us=(\d+) guest_tick=(\d+)', 'Compiled')
$pollRe = [regex]::new('\[XamInputGetState\] user=(\d+) flags=(0x[0-9a-fA-F]+) st=(0x[0-9a-fA-F]+) buttons=(0x[0-9a-fA-F]+) lt=(\d+) rt=(\d+) lx=(-?\d+) ly=(-?\d+) rx=(-?\d+) ry=(-?\d+) guest_tick=(\d+)', 'Compiled')
$startMarker = 'XMPSetPlaybackController(00000000, 00000001)'
$stopMarker = 'XMPSetPlaybackController(00000000, 00000000)'
$probeBit = 0x1000
$knownMask = @(0x1000)  # the only button the probes press

$checks = New-Object System.Collections.ArrayList
$notes = New-Object System.Collections.ArrayList

function Add-Check([string]$Id, [string]$What, [string]$Expected, [string]$Measured, [bool]$Ok) {
    $checks.Add([pscustomobject]@{
        Id = $Id; What = $What; Expected = $Expected; Measured = $Measured
        Status = $(if ($Ok) { "PASS" } else { "FAIL" })
    }) | Out-Null
}

function Add-Note([string]$Text) { $notes.Add($Text) | Out-Null }
function Format-Time($t) { if ($null -eq $t) { return "n/a" } return $t.ToString("HH:mm:ss.fff") }

function Format-Stamp($t) { if ($null -eq $t) { return "" } return $t.ToString("yyyy-MM-dd HH:mm:ss.fff") }

function Get-Median($sorted) {
    if ($sorted.Count -eq 0) { return 0.0 }
    $n = $sorted.Count
    if ($n % 2 -eq 1) { return [double]$sorted[[int](($n - 1) / 2)] }
    return ([double]$sorted[$n / 2 - 1] + [double]$sorted[$n / 2]) / 2.0
}

function Get-Quantile($sorted, [double]$q) {
    if ($sorted.Count -eq 0) { return 0.0 }
    $i = [int][Math]::Floor($q * $sorted.Count)
    if ($i -ge $sorted.Count) { $i = $sorted.Count - 1 }
    if ($i -lt 0) { $i = 0 }
    return [double]$sorted[$i]
}

function Get-FilesChronological([string]$LogDir) {
    # Same rule as the driver: order by the first timestamp in the file so log
    # rotation naming cannot reorder the run.
    $rows = New-Object System.Collections.ArrayList
    foreach ($f in (Get-ChildItem (Join-Path $LogDir "*.log") -ErrorAction SilentlyContinue)) {
        $first = ""
        try {
            $sr = [IO.File]::OpenText($f.FullName)
            try { $first = $sr.ReadLine() } finally { $sr.Close() }
        } catch { }
        $rows.Add([pscustomobject]@{ File = $f; First = $first }) | Out-Null
    }
    return @($rows | Sort-Object First)
}

function Get-LineTime([string]$Line) {
    # "[YYYY-MM-DD HH:MM:SS.mmm] ..." -> local DateTime, or $null.
    # "[", 23 timestamp characters, "]" -> the bracket closes at index 24.
    if ($Line.Length -lt 25) { return $null }
    if ($Line[0] -ne '[') { return $null }
    if ($Line[24] -ne ']') { return $null }
    try {
        return [datetime]::ParseExact($Line.Substring(1, 23), "yyyy-MM-dd HH:mm:ss.fff", $null)
    } catch { return $null }
}

# Sweep the log set once for the cheap, whole-run facts: per-file time ranges,
# the song's playback envelope, and whether anything fatal was logged.
function Get-LogSweep([string]$LogDir) {
    $files = Get-FilesChronological $LogDir
    $starts = New-Object System.Collections.ArrayList
    $stops = New-Object System.Collections.ArrayList
    $info = New-Object System.Collections.ArrayList
    $fatal = $false
    $bytes = 0
    $lines = 0
    foreach ($row in $files) {
        $bytes += $row.File.Length
        $firstTime = $null; $lastTime = $null; $lastRaw = $null; $count = 0
        foreach ($line in [IO.File]::ReadLines($row.File.FullName)) {
            $count++
            # File bounds come from the first and last timestamped line, not from
            # the markers: the overlap test below needs the real extent.
            if ($null -eq $firstTime) { $firstTime = Get-LineTime $line }
            $lastRaw = $line
            if (($line.IndexOf($startMarker) -lt 0) -and ($line.IndexOf($stopMarker) -lt 0) -and ($line.IndexOf('[FATAL]') -lt 0)) { continue }
            $t = Get-LineTime $line
            if ($null -eq $t) { continue }
            if ($line.IndexOf('[FATAL]') -ge 0) { $fatal = $true }
            if ($line.IndexOf($startMarker) -ge 0) { $starts.Add($t) | Out-Null }
            if ($line.IndexOf($stopMarker) -ge 0) { $stops.Add($t) | Out-Null }
        }
        if ($null -ne $lastRaw) { $lastTime = Get-LineTime $lastRaw }
        $lines += $count
        if ($null -eq $firstTime) {
            # Timestamp-less files still need bounds for the overlap test.
            $firstTime = [datetime]::MinValue; $lastTime = [datetime]::MaxValue
        } elseif ($null -eq $lastTime) {
            $lastTime = $firstTime
        }
        $info.Add([pscustomobject]@{
            Name = $row.File.Name; Path = $row.File.FullName; Bytes = $row.File.Length
            Lines = $count; First = $firstTime; Last = $lastTime
        }) | Out-Null
    }
    return [pscustomobject]@{
        Files = $info; Starts = $starts; Stops = $stops; Fatal = $fatal
        Bytes = $bytes; Lines = $lines
    }
}

# Walk the traces inside a window and return everything the checks need.
function Measure-Traces([object]$Sweep, [datetime]$WindowStart, [datetime]$WindowEnd, [array]$ProbeWindows) {
    $frames = @{}
    foreach ($name in "VdSwap", "XE_SWAP") {
        $frames[$name] = @{
            Count = 0; FirstTs = $null; LastTs = $null; FirstTick = 0; LastTick = 0
            FirstN = 0; LastN = 0; Missing = 0; Dup = 0; Dt = (New-Object System.Collections.ArrayList)
        }
    }
    $poll = @{
        Count = 0; FirstTs = $null; LastTs = $null; FirstTick = 0; LastTick = 0
        LastTickSeen = -1; Gaps = (New-Object System.Collections.ArrayList)
        GapMaxMs = 0.0; NonUser0 = 0; Unsuccessful = 0; UnexpectedMasks = @{}; Capture = (New-Object System.Collections.ArrayList)
    }
    $nonUser0Buttons = 0

    foreach ($fi in $Sweep.Files) {
        if ($fi.Last -lt $WindowStart -or $fi.First -gt $WindowEnd) { continue }
        foreach ($line in [IO.File]::ReadLines($fi.Path)) {
            $isFrame = $false; $isPoll = $false
            if ($line.IndexOf('[VdSwap]') -ge 0) { $isFrame = $true }
            elseif ($line.IndexOf('[XE_SWAP]') -ge 0) { $isPoll = $false; $isFrame = $true }
            if ($line.IndexOf('[XamInputGetState]') -ge 0) { $isPoll = $true }
            if (-not ($isFrame -or $isPoll)) { continue }
            $t = Get-LineTime $line
            if ($null -eq $t) { continue }
            if ($t -lt $WindowStart -or $t -gt $WindowEnd) { continue }

            if ($isFrame) {
                $m = $vdRe.Match($line)
                $key = "VdSwap"
                if (-not $m.Success) { $m = $xeRe.Match($line); $key = "XE_SWAP" }
                if (-not $m.Success) { continue }
                $s = $frames[$key]
                $n = [int64]$m.Groups[1].Value
                $dt = [double]$m.Groups[2].Value / 1000.0
                $tick = [int64]$m.Groups[3].Value
                if ($s.Count -eq 0) {
                    $s.FirstTs = $t; $s.FirstTick = $tick; $s.FirstN = $n
                } else {
                    $step = $n - $s.LastN
                    if ($step -gt 1) { $s.Missing += [int]($step - 1) }
                    elseif ($step -le 0) { $s.Dup += 1 }
                    $s.Dt.Add($dt) | Out-Null
                }
                $s.Count++; $s.LastTs = $t; $s.LastTick = $tick; $s.LastN = $n
            } else {
                $m = $pollRe.Match($line)
                if (-not $m.Success) { continue }
                $user = [int]$m.Groups[1].Value
                $buttons = [int]("0x" + $m.Groups[4].Value.Substring(2))
                $status = $m.Groups[3].Value
                $tick = [int64]$m.Groups[11].Value
                if ($user -ne 0) { $poll.NonUser0++; continue }
                # A failed fetch is the title asking whether a controller exists,
                # not a poll of a pad that exists.
                if ($status -ne "0x0") { $poll.Unsuccessful++; continue }
                if ($poll.Count -gt 0 -and $poll.LastTickSeen -gt 0) {
                    $gapTicks = $tick - $poll.LastTickSeen
                    if ($gapTicks -ge 0) { $poll.Gaps.Add([double]$gapTicks) | Out-Null }
                }
                if ($poll.Count -eq 0) { $poll.FirstTs = $t; $poll.FirstTick = $tick }
                $poll.Count++; $poll.LastTs = $t; $poll.LastTick = $tick; $poll.LastTickSeen = $tick

                $inCapture = $false
                foreach ($pw in $ProbeWindows) {
                    if ($t -ge $pw.From -and $t -le $pw.To) { $inCapture = $true; break }
                }
                if ($inCapture) {
                    $poll.Capture.Add([pscustomobject]@{ Ts = $t; Buttons = $buttons; Tick = $tick }) | Out-Null
                    if ($buttons -ne 0) {
                        $unexpected = $buttons
                        foreach ($known in $knownMask) { $unexpected = $unexpected -band (-bnot $known) }
                        if ($unexpected -ne 0) {
                            $key = ("0x{0:X4}" -f $unexpected)
                            if ($poll.UnexpectedMasks.ContainsKey($key)) { $poll.UnexpectedMasks[$key]++ }
                            else { $poll.UnexpectedMasks[$key] = 1 }
                        }
                    }
                }
            }
        }
    }
    return [pscustomobject]@{ Frames = $frames; Poll = $poll }
}

function New-SyntheticRun([string]$Path, [switch]$Broken) {
    # A healthy run paces one frame per 16.667 ms and polls every 4 ms; the
    # broken one paces at 40 ms with a 300 ms stall every 20 frames and polls
    # every 500 ms. Both carry the same song envelope and probe schedule.
    New-Item -ItemType Directory -Force -Path (Join-Path $Path "logs") | Out-Null
    $base = [datetime]::ParseExact("2026-09-20 12:00:00.000", "yyyy-MM-dd HH:mm:ss.fff", $null)
    $frameMs = if ($Broken) { 40.0 } else { 1000.0 / 60.0 }
    $stallMs = 300.0
    $pollMs = if ($Broken) { 500.0 } else { 4.0 }
    $songSec = 90.0
    $sb = New-Object System.Text.StringBuilder
    $guestFreq = 50000000.0

    function Fmt([datetime]$t) { return "[" + $t.ToString("yyyy-MM-dd HH:mm:ss.fff") + "] [trace] [krnl] [t100] " }

    $sb.AppendLine((Fmt $base) + "Loaded config: synthetic") | Out-Null
    $sb.AppendLine((Fmt ($base.AddSeconds(1))) + "XMPSetPlaybackController(00000000, 00000001)") | Out-Null

    $songStart = $base.AddSeconds(1)
    $tick = [int64]($guestFreq * 100)
    $frames = New-Object System.Collections.ArrayList
    $t = 0.0
    while ($t -lt $songSec * 1000) {
        $frames.Add($t) | Out-Null
        $t += $frameMs
        if ($Broken -and ($frames.Count % 20) -eq 0) { $t += $stallMs }
    }
    # Probes: ten 100 ms presses every 8 s, the last one short in the broken run.
    $probes = New-Object System.Collections.ArrayList
    $probeRows = New-Object System.Collections.ArrayList
    for ($i = 1; $i -le 10; $i++) {
        $down = 2000.0 + ($i - 1) * 8000.0
        $hold = 100.0
        if ($Broken -and $i -eq 7) { $hold = 30.0 }
        $probes.Add([pscustomobject]@{ Index = $i; DownMs = $down; UpMs = $down + $hold; Hold = $hold }) | Out-Null
        $dl = $songStart.AddMilliseconds($down)
        $ul = $songStart.AddMilliseconds($down + $hold)
        $probeRows.Add([pscustomobject]@{
            Index = $i; Vk = 32; Button = "A"; ButtonBit = "0x1000"
            RequestedHoldMs = [int]$hold; ActualHoldMs = [int]$hold
            DownLocal = $dl.ToString("yyyy-MM-dd HH:mm:ss.fff")
            UpLocal = $ul.ToString("yyyy-MM-dd HH:mm:ss.fff")
            ForegroundBefore = $true; Refocused = $false
        }) | Out-Null
    }

    $events = New-Object System.Collections.ArrayList
    $fi = 0
    foreach ($fm in $frames) {
        $events.Add([pscustomobject]@{ Ms = $fm; Kind = "frame"; Idx = $fi }) | Out-Null
        $fi++
    }
    $t = 0.0
    while ($t -lt $songSec * 1000) {
        $events.Add([pscustomobject]@{ Ms = $t; Kind = "poll"; Idx = 0 }) | Out-Null
        $t += $pollMs
    }
    $sorted = $events | Sort-Object Ms
    $n = 0
    $lastPollTick = 0
    foreach ($e in $sorted) {
        $ts = $songStart.AddMilliseconds($e.Ms)
        if ($e.Kind -eq "frame") {
            $n++
            $tick = [int64]($guestFreq * (100 + $e.Ms / 1000.0))
            $dtUs = if ($n -eq 1) { 0 } else { [int64]($frameMs * 1000) }
            if ($Broken -and ($n - 1) % 20 -eq 0 -and $n -gt 1) { $dtUs = [int64]($stallMs * 1000) }
            $sb.AppendLine((Fmt $ts) + "[VdSwap] n=$n dt_us=$dtUs guest_tick=$tick") | Out-Null
            $sb.AppendLine((Fmt $ts) + "[XE_SWAP] n=$n dt_us=$dtUs guest_tick=$tick") | Out-Null
        } else {
            $tick = [int64]($guestFreq * (100 + $e.Ms / 1000.0))
            $buttons = 0
            foreach ($p in $probes) {
                if ($p.DownMs -le $e.Ms -and $e.Ms -lt $p.UpMs) {
                    # The broken run polls every 500 ms, so a 30 ms press lands
                    # between polls more often than not.
                    if (-not ($Broken -and $p.Hold -lt 50)) { $buttons = $probeBit }
                }
            }
            $sb.AppendLine((Fmt $ts) + "[XamInputGetState] user=0 flags=0x1 st=0x0 buttons=$('0x{0:x4}' -f $buttons) lt=0 rt=0 lx=0 ly=0 rx=0 ry=0 guest_tick=$tick") | Out-Null
            $lastPollTick = $tick
        }
    }
    $stopTs = $songStart.AddSeconds($songSec)
    $sb.AppendLine((Fmt $stopTs) + "XMPSetPlaybackController(00000000, 00000000)") | Out-Null
    [IO.File]::WriteAllText((Join-Path $Path "logs\log01-synthetic.log"), $sb.ToString())

    $run = [ordered]@{
        Tag = "self-test"; Vsync = "on"; LogLevel = "trace"; Gameplay = $true
        Boot = $true; SongList = $true; SongEnd = $true; Clean = $true; Fatal = $false
        SongStartLocal = $songStart.ToString("yyyy-MM-dd HH:mm:ss.fff")
        SongStopLocal = $stopTs.ToString("yyyy-MM-dd HH:mm:ss.fff")
        SongSeconds = [int]$songSec; ProbeHoldsMs = "100"; ProbeCount = 10
        Notes = "synthetic"; LogFiles = @("log01-synthetic.log"); LogBytes = 0
    }
    [pscustomobject]$run | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Path "run.json")
    $probeRows | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Path "injections.json")
}

function Invoke-Analysis([string]$Path, [string]$OutPath, [string]$TagOverride) {
    $checks.Clear() | Out-Null
    $notes.Clear() | Out-Null

    if (-not (Test-Path $Path)) { throw "measurement directory not found: $Path" }
    if (-not $OutPath) { $OutPath = $Path }
    if (-not $TagOverride) { $TagOverride = Split-Path -Leaf (Resolve-Path $Path).Path }
    if (-not $InjectionsPath) { $InjectionsPath = Join-Path $Path "injections.json" }

    $runJson = Join-Path $Path "run.json"
    $run = $null
    if (Test-Path $runJson) { $run = Get-Content -LiteralPath $runJson -Raw | ConvertFrom-Json }
    $vsync = if ($run -and $run.Vsync) { [string]$run.Vsync } else { "" }

    $sweep = Get-LogSweep (Join-Path $Path "logs")
    if ($sweep.Files.Count -eq 0) {
        Add-Check "E1" "log set present" ">=1 log under <dir>\logs" "none" $false
        Add-Note "no logs: was the run started with --log_noisy=true --log_level=trace?"
    } else {
        Add-Check "E1" "log set present" ">=1 log under <dir>\logs" `
            ("{0} file(s), {1:N1} MB, {2} lines" -f $sweep.Files.Count, ($sweep.Bytes / 1MB), $sweep.Lines) $true
    }
    if ($sweep.Fatal) { Add-Note "the log itself contains [FATAL]." }

    $probes = @()
    if (Test-Path $InjectionsPath) {
        # ForEach-Object unrolls the parsed JSON array; in Windows PowerShell 5.1
        # ConvertFrom-Json emits the array itself as a single object.
        $probes = @((Get-Content -LiteralPath $InjectionsPath -Raw | ConvertFrom-Json) | ForEach-Object { $_ })
    } else {
        Add-Note "no injections.json: probe visibility cannot be checked."
    }

    # The song envelope is the last start marker that no stop marker closes: the
    # song list plays previews, which open and close their own envelope, and the
    # driver waits 20 s for one that stays open for exactly that reason -- so
    # mirror it here instead of trusting the first start marker.
    $openStart = $null; $closedStart = $null; $lastStop = $null; $previews = 0
    $markers = New-Object System.Collections.ArrayList
    foreach ($t in $sweep.Starts) { $markers.Add([pscustomobject]@{ Time = $t; Kind = 'start' }) | Out-Null }
    foreach ($t in $sweep.Stops) { $markers.Add([pscustomobject]@{ Time = $t; Kind = 'stop' }) | Out-Null }
    foreach ($e in @($markers | Sort-Object Time)) {
        if ($e.Kind -eq 'start') {
            if ($null -ne $openStart) { $previews++ }
            $openStart = $e.Time
        } else {
            if ($null -ne $openStart) { $closedStart = $openStart; $previews++ }
            $openStart = $null
            $lastStop = $e.Time
        }
    }
    $envelopeClosed = ($null -eq $openStart)
    $haveEnvelope = ($null -ne $openStart) -or ($null -ne $closedStart)
    Add-Check "E2" "song playback envelope" "a start marker that no stop marker closes" `
        ("{0} start(s), {1} stop(s), {2} preview(s), song start {3}" -f $sweep.Starts.Count, $sweep.Stops.Count, $previews, `
            $(if ($envelopeClosed) { Format-Time $closedStart } else { Format-Time $openStart })) $haveEnvelope
    if (-not $haveEnvelope) {
        Add-Note "no song start marker: the run never reached gameplay, or the marker was filtered out of the log."
    }

    if ($run) {
        $routeOk = [bool]($run.Boot -and $run.SongList -and $run.Gameplay -and $run.Clean -and (-not $run.Fatal))
        Add-Check "E3" "route completed" "Boot+SongList+Gameplay+Clean, no [FATAL]" `
            ("boot={0} list={1} gameplay={2} clean={3} fatal={4}" -f $run.Boot, $run.SongList, $run.Gameplay, $run.Clean, $run.Fatal) $routeOk
    } else {
        Add-Check "E3" "route completed" "run.json from measure_pacing_input.ps1" "run.json missing" $false
    }

    if ($envelopeClosed) {
        $windowStart = $closedStart
        $windowEnd = $lastStop
    } else {
        $windowStart = $openStart
        $windowEnd = $sweep.Files[-1].Last
    }
    if ($previews -gt 0) { Add-Note "$previews song-list preview envelope(s) ignored." }
    if (-not $envelopeClosed -and $haveEnvelope) {
        Add-Note "no stop marker closed the song envelope: the window runs to the end of the log (partial run)."
    }
    $windowSec = 0.0
    if ($haveEnvelope) {
        $windowSec = ($windowEnd - $windowStart).TotalSeconds
        if ($windowSec -lt 0) { $windowSec = 0 }
    }
    Add-Check "E4" "gameplay window length" ">= $MinWindowSec s" ("{0:N1} s" -f $windowSec) ($windowSec -ge $MinWindowSec)

    if (-not $haveEnvelope) {
        return (Write-Verdict $Path $OutPath $TagOverride $vsync $sweep $null $probes $null "" $windowSec $envelopeClosed)
    }

    # Polls are captured only near the probes, so build the capture zones first.
    $probeWindows = New-Object System.Collections.ArrayList
    foreach ($p in $probes) {
        $dn = [datetime]::ParseExact($p.DownLocal, "yyyy-MM-dd HH:mm:ss.fff", $null)
        $up = [datetime]::ParseExact($p.UpLocal, "yyyy-MM-dd HH:mm:ss.fff", $null)
        $probeWindows.Add([pscustomobject]@{ From = $dn.AddMilliseconds(-5); To = $up.AddMilliseconds(150) }) | Out-Null
    }

    $m = Measure-Traces $sweep $windowStart $windowEnd $probeWindows
    return (Write-Verdict $Path $OutPath $TagOverride $vsync $sweep $m $probes $probeWindows (Format-Stamp $windowStart) $windowSec $envelopeClosed)
}

function Write-Verdict([string]$Path, [string]$OutPath, [string]$TagOverride, [string]$vsync,
                       [object]$sweep, [object]$m, [object]$probes, [object]$probeWindows,
                       [string]$windowStartText, [double]$windowSec = 0, [bool]$envelopeClosed = $false) {
    $frames = $null
    $poll = $null
    $frameReport = @{}
    $inputReport = [ordered]@{}
    $probeReport = New-Object System.Collections.ArrayList
    $windowStart = $null
    $windowEnd = $null
    if ($windowStartText) {
        $windowStart = [datetime]::ParseExact($windowStartText, "yyyy-MM-dd HH:mm:ss.fff", $null)
        $windowEnd = $windowStart.AddSeconds($windowSec)
    }

    if ($m) {
        $frames = $m.Frames
        $poll = $m.Poll
        foreach ($name in "VdSwap", "XE_SWAP") {
            $s = $frames[$name]
            $span = 0.0
            if ($s.Count -gt 1) { $span = ($s.LastTs - $s.FirstTs).TotalSeconds }
            $sorted = @($s.Dt | Sort-Object)
            $med = Get-Median $sorted
            $p90 = Get-Quantile $sorted 0.90
            $p99 = Get-Quantile $sorted 0.99
            $max = if ($sorted.Count) { [double]$sorted[-1] } else { 0.0 }
            $hitchLimit = $med * $HitchFactor
            $hitches = @($sorted | Where-Object { $_ -gt $hitchLimit }).Count
            $frameReport[$name] = [pscustomobject]@{
                Count = $s.Count; SpanSec = $span
                Fps = $(if ($span -gt 0) { ($s.Count - 1) / $span } else { 0 })
                MedianMs = $med; P90Ms = $p90; P99Ms = $p99; MaxMs = $max
                HitchLimitMs = $hitchLimit; Hitches = $hitches
                HitchPct = $(if ($sorted.Count) { 100.0 * $hitches / $sorted.Count } else { 0 })
                Missing = $s.Missing; Duplicates = $s.Dup
            }
        }

        $vd = $frames["VdSwap"]; $xe = $frames["XE_SWAP"]
        $framesMinusOne = $xe.Count - 1
        $vdTicks = $vd.LastTick - $vd.FirstTick
        $vdSpanMs = ($vd.LastTs - $vd.FirstTs).TotalMilliseconds
        $guestFreq = if ($vdSpanMs -gt 0) { $vdTicks * 1000.0 / $vdSpanMs } else { 0.0 }

        # Poll gaps in guest ticks, converted with the frequency derived from the
        # frame series so no constant has to be trusted.
        $pollGapsMs = New-Object System.Collections.ArrayList
        if ($guestFreq -gt 0) {
            foreach ($g in $poll.Gaps) { $pollGapsMs.Add(1000.0 * $g / $guestFreq) | Out-Null }
        }
        $pollSorted = @($pollGapsMs | Sort-Object)
        $pollSpanSec = 0.0
        if ($poll.Count -gt 1) { $pollSpanSec = ($poll.LastTs - $poll.FirstTs).TotalSeconds }
        $inputReport = [ordered]@{
            Polls = $poll.Count
            NonUser0Polls = $poll.NonUser0
            UnsuccessfulPolls = $poll.Unsuccessful
            SpanSec = $pollSpanSec
            RatePerSec = $(if ($pollSpanSec -gt 0) { ($poll.Count - 1) / $pollSpanSec } else { 0 })
            GapMedianMs = (Get-Median $pollSorted)
            GapP99Ms = (Get-Quantile $pollSorted 0.99)
            GapMaxMs = $(if ($pollSorted.Count) { [double]$pollSorted[-1] } else { 0 })
            GuestTickHz = $guestFreq
        }

        # --- frame checks ---------------------------------------------------
        Add-Check "F1" "guest frame traces present" ">= 100 VdSwap + XE_SWAP in window" `
            ("VdSwap={0} XE_SWAP={1}" -f $vd.Count, $xe.Count) ($vd.Count -ge 100 -and $xe.Count -ge 100)
        Add-Check "F2" "log integrity (frame counters)" "no missing/duplicate trace lines" `
            ("VdSwap missing={0} dup={1}; XE_SWAP missing={2} dup={3}" -f $vd.Missing, $vd.Dup, $xe.Missing, $xe.Dup) `
            ($vd.Missing -eq 0 -and $vd.Dup -eq 0 -and $xe.Missing -eq 0 -and $xe.Dup -eq 0)
        $r = $frameReport["XE_SWAP"]
        Add-Check "F3" "presented frame cadence" ("median dt <= {0:N1} ms" -f $MaxMedianDtMs) `
            ("median {0:N2} ms ({1:N1} fps)" -f $r.MedianMs, $r.Fps) ($r.MedianMs -le $MaxMedianDtMs)
        Add-Check "F4" "frame time tail" ("p99 dt <= {0:N1} ms" -f $MaxP99DtMs) `
            ("p90 {0:N2} ms, p99 {1:N2} ms, max {2:N1} ms" -f $r.P90Ms, $r.P99Ms, $r.MaxMs) ($r.P99Ms -le $MaxP99DtMs)
        Add-Check "F5" "hitch budget" ("frames > {0}x median <= {1:N1}%" -f $HitchFactor, $MaxHitchPct) `
            ("{0} hitch(es) of {1} frames ({2:N2}%)" -f $r.Hitches, $framesMinusOne, $r.HitchPct) ($r.HitchPct -le $MaxHitchPct)
        $driftCount = [Math]::Abs($vd.Count - $xe.Count)
        $driftFps = [Math]::Abs($frameReport["VdSwap"].Fps - $r.Fps)
        Add-Check "F6" "submit/present agree" "|nVdSwap - nXE_SWAP| <= 2 and |fps diff| <= 2" `
            ("dn={0}, dfps={1:N2}" -f $driftCount, $driftFps) ($driftCount -le 2 -and $driftFps -le 2)
        if ($vsync -eq "on") {
            $pacOk = ($r.MedianMs -ge 14.0 -and $r.MedianMs -le 20.0)
            Add-Check "F7" "guest paced to the 60 Hz vblank" "median dt in [14.0, 20.0] ms" `
                ("median {0:N2} ms" -f $r.MedianMs) $pacOk
        } else {
            Add-Note ("vsync={0}: no fixed-rate assertion; the median dt is reported as the contrast case." -f $vsync)
        }

        # --- input checks ---------------------------------------------------
        Add-Check "I1" "input polling rate" (">= {0:N0} polls/s" -f $MinPollRate) `
            ("{0:N1} polls/s ({1} polls)" -f $inputReport.RatePerSec, $poll.Count) ($inputReport.RatePerSec -ge $MinPollRate)
        Add-Check "I2" "worst input poll gap" ("p99 <= {0:N0} ms, max <= {1:N0} ms" -f $MaxPollGapP99Ms, $MaxPollGapMs) `
            ("p99 {0:N2} ms, max {1:N1} ms" -f $inputReport.GapP99Ms, $inputReport.GapMaxMs) `
            ($inputReport.GapP99Ms -le $MaxPollGapP99Ms -and $inputReport.GapMaxMs -le $MaxPollGapMs)
        if ($guestFreq -le 0) { Add-Note "could not derive the guest tick frequency; poll gaps were measured from microseconds only." }

        $capture = $poll.Capture
        $asserted = 0; $assertedMissed = 0; $char = 0; $charSeen = 0; $invalid = 0
        foreach ($p in $probes) {
            $dn = [datetime]::ParseExact($p.DownLocal, "yyyy-MM-dd HH:mm:ss.fff", $null)
            $up = [datetime]::ParseExact($p.UpLocal, "yyyy-MM-dd HH:mm:ss.fff", $null)
            $inWindow = ($null -ne $windowStart -and $dn -ge $windowStart -and $up -le $windowEnd)
            $seen = @($capture | Where-Object { $_.Ts -ge $dn -and $_.Ts -le $up.AddMilliseconds(150) -and ($_.Buttons -band $probeBit) })
            $latency = $null
            if ($seen.Count -gt 0) { $latency = ($seen[0].Ts - $dn).TotalMilliseconds }
            # The key events reach the guest only through the game window's own
            # key events, so a press the host did not deliver cleanly is not
            # evidence about the guest and is discarded instead of asserted.
            # Runs recorded before the driver sampled this carry no flag and are
            # taken at face value.
            $delivered = $true
            if ($null -ne $p.ForegroundStable) { $delivered = [bool]$p.ForegroundStable }
            $isAsserted = ($inWindow -and $p.ActualHoldMs -ge $AssertHoldMs -and $delivered)
            if ($inWindow -and -not $delivered) { $invalid++ }
            elseif ($isAsserted) { $asserted++; if ($seen.Count -eq 0) { $assertedMissed++ } }
            elseif ($inWindow) { $char++; if ($seen.Count -gt 0) { $charSeen++ } }
            $probeReport.Add([pscustomobject]@{
                Index = $p.Index; HoldMs = $p.ActualHoldMs; InWindow = $inWindow
                Delivered = $delivered; Observed = ($seen.Count -gt 0); PollsSeen = $seen.Count
                LatencyMs = $latency; Asserted = $isAsserted; Refocused = $p.Refocused
            }) | Out-Null
        }
        if ($invalid -gt 0) {
            Add-Note ("{0} press(es) were discarded: the game window lost the foreground during the hold, so the key event could not reach the guest. Those runs were repeated by the driver." -f $invalid)
        }
        # The driver records the realized key-down to key-up time, which is what
        # the guest sees; the requested bucket is the harness's target. They are
        # not the same number, and the record says which one is asserted.
        $requested = @($probes | ForEach-Object { $_.RequestedHoldMs } | Where-Object { $null -ne $_ } | Sort-Object -Unique)
        $worstDev = 0.0
        foreach ($p in $probes) {
            if ($null -eq $p.RequestedHoldMs) { continue }
            $dev = [Math]::Abs($p.ActualHoldMs - $p.RequestedHoldMs)
            if ($dev -gt $worstDev) { $worstDev = $dev }
        }
        if ($requested.Count -gt 0 -and $probeReport.Count -gt 0 -and $worstDev -gt 5) {
            $realized = @($probeReport | ForEach-Object { $_.HoldMs })
            Add-Note ("requested hold buckets {0} ms realized as {1:N0}-{2:N0} ms key-down to key-up (an injected key event costs ~3 ms and short targets are overshot while the game runs), so only presses realized >= {3:N0} ms are asserted and the shorter bucket is characterization." -f `
                    ($requested -join "/"), ($realized | Measure-Object -Minimum).Minimum, ($realized | Measure-Object -Maximum).Maximum, $AssertHoldMs)
        }
        Add-Check "I3" "every scripted press reached the guest" `
            ("all in-song presses held >= {0:N0} ms with the window foreground throughout are observed" -f $AssertHoldMs) `
            ("{0} asserted, {1} missed, {2} discarded (of {3} probes)" -f $asserted, $assertedMissed, $invalid, $probes.Count) `
            ($asserted -gt 0 -and $assertedMissed -eq 0)
        $latencies = @($probeReport | Where-Object { $null -ne $_.LatencyMs } | ForEach-Object { $_.LatencyMs })
        $latMax = if ($latencies.Count) { ($latencies | Measure-Object -Maximum).Maximum } else { 0.0 }
        Add-Check "I4" "key-down to guest visibility" ("first observing poll <= {0:N0} ms after key-down" -f $MaxProbeLatencyMs) `
            ("worst {0:N1} ms over {1} observed press(es)" -f $latMax, $latencies.Count) `
            ($latencies.Count -gt 0 -and $latMax -le $MaxProbeLatencyMs)
        $unexpected = @()
        foreach ($k in $poll.UnexpectedMasks.Keys) { $unexpected += ("{0}x{1}" -f $k, $poll.UnexpectedMasks[$k]) }
        Add-Check "I5" "no stray buttons while probing" "only the probe button is ever set" `
            $(if ($unexpected.Count) { $unexpected -join ", " } else { "none" }) ($unexpected.Count -eq 0)
        if ($char -gt 0) {
            Add-Note ("sub-{0:N0} ms presses (characterization, not asserted): {1} of {2} were seen - a press shorter than a poll gap can fall between polls." -f $AssertHoldMs, $charSeen, $char)
        }
        Add-Check "I6" "probe coverage" (">= {0} in-song presses" -f $MinProbes) `
            ("{0} in-song probe(s) of {1} recorded" -f ($asserted + $char), $probes.Count) `
            (($asserted + $char) -ge $MinProbes)
    }

    $failed = @($checks | Where-Object { $_.Status -eq "FAIL" })
    $verdict = if ($checks.Count -eq 0 -or $failed.Count -gt 0) { "FAIL" } else { "PASS" }

    $report = [ordered]@{
        Tag = $TagOverride; Vsync = $vsync
        Verdict = $verdict
        WindowStart = $windowStartText
        WindowEnd = $(if ($null -ne $windowEnd) { $windowEnd.ToString("yyyy-MM-dd HH:mm:ss.fff") } else { "" })
        WindowSeconds = $windowSec
        SongEnvelopeClosed = $envelopeClosed
        LogFiles = @($sweep.Files | ForEach-Object { $_.Name })
        LogBytes = $sweep.Bytes
        Checks = @($checks)
        Frames = $(if ($m) { [pscustomobject]$frameReport } else { $null })
        Input = $inputReport
        Probes = @($probeReport)
        Notes = @($notes)
    }

    if (-not (Test-Path $OutPath)) { New-Item -ItemType Directory -Force -Path $OutPath | Out-Null }
    [pscustomobject]$report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $OutPath "pacing-input.json")

    # --- markdown --------------------------------------------------------
    $md = New-Object System.Collections.ArrayList
    $md.Add("# Frame pacing and input polling: $TagOverride") | Out-Null
    $md.Add("") | Out-Null
    $md.Add("Verdict: **$verdict**" + $(if ($failed.Count) { " ($($failed.Count) check(s) failed: $(($failed | ForEach-Object { $_.Id }) -join ', '))" } else { "" })) | Out-Null
    $md.Add("") | Out-Null
    $md.Add("- vsync setting: ``$vsync``") | Out-Null
    $md.Add("- gameplay window: $($report.WindowStart) -> $($report.WindowEnd) ($([Math]::Round($windowSec, 1)) s, envelope closed: $envelopeClosed)") | Out-Null
    $md.Add("- log set: $($sweep.Files.Count) file(s), $([Math]::Round($sweep.Bytes / 1MB, 1)) MB, $($sweep.Lines) lines") | Out-Null
    $md.Add("") | Out-Null
    $md.Add("| Check | What | Expected | Measured | Status |") | Out-Null
    $md.Add("|---|---|---|---|---|") | Out-Null
    foreach ($c in $checks) {
        $md.Add(("| {0} | {1} | {2} | {3} | {4} |" -f $c.Id, $c.What, $c.Expected, $c.Measured, $c.Status)) | Out-Null
    }
    if ($m) {
        $md.Add("") | Out-Null
        $md.Add("## Frames") | Out-Null
        $md.Add("") | Out-Null
        $md.Add("| Series | Frames | fps | median ms | p90 ms | p99 ms | max ms | hitches > 2.5x med | missing/dup |") | Out-Null
        $md.Add("|---|---|---|---|---|---|---|---|---|") | Out-Null
        foreach ($name in "VdSwap", "XE_SWAP") {
            $r = $frameReport[$name]
            $md.Add(("| {0} | {1} | {2:N1} | {3:N2} | {4:N2} | {5:N2} | {6:N1} | {7} ({8:N2}%) | {9}/{10} |" -f `
                        $name, $r.Count, $r.Fps, $r.MedianMs, $r.P90Ms, $r.P99Ms, $r.MaxMs, $r.Hitches, $r.HitchPct, $r.Missing, $r.Duplicates)) | Out-Null
        }
        $md.Add("") | Out-Null
        $md.Add("*VdSwap* is the guest's frame submission, *XE_SWAP* the host present; dt comes from the host-microsecond delta recorded next to each event. Guest tick frequency derived from the frame series: $([Math]::Round($inputReport.GuestTickHz / 1e6, 3)) MHz.") | Out-Null
        $md.Add("") | Out-Null
        $md.Add("## Input polling") | Out-Null
        $md.Add("") | Out-Null
        $md.Add("- successful polls for user 0: $($inputReport.Polls) in $([Math]::Round($inputReport.SpanSec, 1)) s ($([Math]::Round($inputReport.RatePerSec, 1))/s)") | Out-Null
        $md.Add("- poll gap: median $([Math]::Round($inputReport.GapMedianMs, 2)) ms, p99 $([Math]::Round($inputReport.GapP99Ms, 2)) ms, max $([Math]::Round($inputReport.GapMaxMs, 1)) ms") | Out-Null
        $md.Add("- ignored: $($inputReport.NonUser0Polls) poll(s) for a user other than 0, $($inputReport.UnsuccessfulPolls) failed fetch(es)") | Out-Null
        if ($probeReport.Count) {
            $md.Add("") | Out-Null
            $md.Add("## Injected presses") | Out-Null
            $md.Add("") | Out-Null
            $md.Add("| # | hold ms | in song | delivered | observed | polls seeing A | latency ms | asserted |") | Out-Null
            $md.Add("|---|---|---|---|---|---|---|---|") | Out-Null
            foreach ($pr in $probeReport) {
                $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} |" -f $pr.Index, $pr.HoldMs, $pr.InWindow, $pr.Delivered, $pr.Observed, $pr.PollsSeen,
                            $(if ($null -ne $pr.LatencyMs) { "{0:N0}" -f $pr.LatencyMs } else { "n/a" }), $pr.Asserted)) | Out-Null
            }
        }
    }
    if ($notes.Count) {
        $md.Add("") | Out-Null
        $md.Add("## Notes") | Out-Null
        $md.Add("") | Out-Null
        foreach ($n in $notes) { $md.Add("- $n") | Out-Null }
    }
    $md.Add("") | Out-Null
    $md.Add("Thresholds: median dt <= $MaxMedianDtMs ms, p99 dt <= $MaxP99DtMs ms, hitches (> $HitchFactor x median) <= $MaxHitchPct%, poll rate >= $MinPollRate/s, poll gap p99 <= $MaxPollGapP99Ms ms and max <= $MaxPollGapMs ms, presses held >= $AssertHoldMs ms all observed within $MaxProbeLatencyMs ms of key-down.") | Out-Null
    [IO.File]::WriteAllLines((Join-Path $OutPath "pacing-input.md"), $md.ToArray())

    return [pscustomobject]@{ Verdict = $verdict; Report = $report; Checks = @($checks); Failed = $failed }
}

# ------------------------------------------------------------------- driver ---

if ($SelfTest) {
    $tmp = Join-Path ([IO.Path]::GetTempPath()) ("m5-pacing-selftest-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    try {
        $good = Join-Path $tmp "good"
        $bad = Join-Path $tmp "bad"
        New-SyntheticRun $good
        New-SyntheticRun $bad -Broken

        $rGood = Invoke-Analysis $good $good "self-test-good"
        $rBad = Invoke-Analysis $bad $bad "self-test-bad"
        $badIds = @($rBad.Failed | ForEach-Object { $_.Id })

        Write-Host "self-test good run  -> $($rGood.Verdict) ($(@($rGood.Failed).Count) failed)"
        Write-Host "self-test bad run   -> $($rBad.Verdict) (failed: $($badIds -join ', '))"
        $ok = $true
        if ($rGood.Verdict -ne "PASS") {
            Write-Host "  !! the healthy synthetic run did not pass:"
            foreach ($c in $rGood.Failed) { Write-Host "     $($c.Id): $($c.What) expected $($c.Expected), measured $($c.Measured)" }
            $ok = $false
        }
        if ($rBad.Verdict -ne "FAIL") { Write-Host "  !! the broken synthetic run passed"; $ok = $false }
        foreach ($id in "F4", "F5", "I2", "I3") {
            if ($badIds -notcontains $id) { Write-Host "  !! the broken run did not fail $id"; $ok = $false }
        }
        Write-Host $(if ($ok) { "self-test PASS" } else { "self-test FAIL" })
        if (-not $ok) { exit 1 }
        exit 0
    } finally {
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    }
}

if (-not $Dir) { throw "-Dir is required (or use -SelfTest)" }
$final = Invoke-Analysis $Dir $OutDir $Tag
Write-Host "`n=== pacing/input audit: $($final.Report.Tag) ==="
foreach ($c in $final.Checks) {
    Write-Host ("  [{0}] {1,-4} {2} - {3}" -f $c.Status, $c.Id, $c.Measured, $c.What)
}
if ($final.Report.Frames) {
    foreach ($name in "VdSwap", "XE_SWAP") {
        $f = $final.Report.Frames.$name
        if ($f) {
            Write-Host ("  {0,-8} {1,6} frames {2,7:N1} fps  median {3,6:N2} ms  p99 {4,6:N2} ms  max {5,6:N1} ms  hitches {6}" -f `
                    $name, $f.Count, $f.Fps, $f.MedianMs, $f.P99Ms, $f.MaxMs, $f.Hitches)
        }
    }
    $i = $final.Report.Input
    Write-Host ("  polls    {0,6}        {1,7:N1}/s   gap median {2,5:N2} ms  p99 {3,5:N2} ms  max {4,6:N1} ms" -f `
            $i.Polls, $i.RatePerSec, $i.GapMedianMs, $i.GapP99Ms, $i.GapMaxMs)
}
foreach ($n in $final.Report.Notes) { Write-Host "  note: $n" }
Write-Host "verdict: $($final.Verdict)"
exit $(if ($final.Verdict -eq "PASS") { 0 } else { 1 })
