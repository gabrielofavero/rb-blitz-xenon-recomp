# Milestone 5: measure frame pacing and input polling on a real song.
#
# The milestone's remaining stability question is not answerable from a vanilla
# log: nothing in it prints a frame time or a poll rate. This driver produces the
# evidence instead of an opinion:
#
#   * it runs the documented launch-to-results route (same OCR-gated transitions
#     as scripts/acceptance_song.ps1) with the SDK's swap/poll traces on
#     (patches/rexglue-sdk/0004, noisy-trace gated: [VdSwap] per guest frame
#     submitted, [XE_SWAP] per frame presented, [XamInputGetState] per poll);
#   * while the song plays it injects timed pad-button probes at three hold
#     lengths (8 ms / 30 ms / 90 ms) and records the wall-clock time of each
#     press, so scripts/audit_pacing_input.ps1 can tell how long a press has to
#     be held to be visible to a guest poll;
#   * it copies the run's whole rotated log set next to the injection record.
#
# Usage:
#   .\scripts\measure_pacing_input.ps1                     # vsync on, full song
#   .\scripts\measure_pacing_input.ps1 -Vsync off          # uncapped contrast
#   .\scripts\measure_pacing_input.ps1 -Pilot -PilotSeconds 45   # short pipeline check
#
# Output: out/m5-pacing/<tag>/{run.json, injections.json, logs/*.log} and a
# pass/fail summary for the route itself. Exit code 0 means the route completed
# and the evidence was written; the pacing verdict belongs to the analyzer.
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m5-pacing",
    [string]$GameRoot,
    # GPU vblank emulation, the SDK's `vsync` cvar. "on" is the shipping
    # default; "off" runs the guest's frame loop without the vblank pace as the
    # contrast case.
    [ValidateSet("on", "off")][string]$Vsync = "on",
    # Subdirectory to write under -OutDir; defaults to the vsync setting.
    [string]$Tag,
    # 1-based row in the artist-sorted song list: 1 Random Song, 2 One Week,
    # 3 These Days, 4 Death on Two Legs.
    [int]$Song = 3,
    [string]$SongName = "THESE DAYS",
    [int]$ProbeCount = 15,
    [int]$ProbeIntervalSec = 12,
    # A probe whose key events were not delivered to the game window is
    # repeated this many times before it is recorded as delivered anyway.
    [int]$ProbeAttempts = 3,
    # The traces live behind the `log_noisy` cvar and the krnl category level;
    # the SDK's own log_level cvar has to allow trace for them to reach disk.
    [string]$LogLevel = "trace",
    # Stop as soon as the probe window is over instead of playing the song out.
    [switch]$Pilot,
    [int]$PilotSeconds = 45,
    [int]$BootTimeoutSec = 180,
    [int]$ScreenTimeoutSec = 90,
    [int]$SongTimeoutSec = 600,
    [int]$CloseWaitSec = 20
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work "rb_blitz.exe"
$logs = Join-Path $work "logs"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
if (-not $Tag) { $Tag = "vsync-$Vsync" }
$capDir = Join-Path $root (Join-Path $OutDir $Tag)
$logDir = Join-Path $capDir "logs"
# One directory per run: an evidence set that mixed two runs' logs would confuse
# the marker-envelope walk and the log accounting.
if (Test-Path $capDir) { Remove-Item -Recurse -Force $capDir }
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }

# The pad button the probes press. Space is the MnK default for A
# (mnk_input_driver.cpp keybind_a), and A is the one button the title's menus
# already accept, so a probe cannot derail the run.
$probeVk = 0x20
$probeScan = 0x39
$probeBit = 0x1000
# Round-robin hold lengths: short (below a 60 Hz poll gap), medium, and long
# (comfortably longer than one poll gap).
$probeHoldsMs = @(8, 30, 90)

$startMarker = "XMPSetPlaybackController\(00000000, 00000001\)"
$stopMarker = "XMPSetPlaybackController\(00000000, 00000000\)"

# ---------------------------------------------------------------- helpers ---

if (-not ("RbPaceWin32" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class RbPaceWin32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
}
"@
}

# Same reason as scripts/acceptance_song.ps1: this machine's execution policy is
# Restricted, so every other .ps1 is invoked through a child PowerShell.
function Invoke-ChildScript([string]$Script, [hashtable]$Named) {
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) { $argv += @("-$k", [string]$Named[$k]) }
    $out = & powershell @argv 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$Script failed: $out" }
    return $out
}

function Get-LogFiles {
    @(Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending)
}

function Get-LogText([string]$Path) {
    if (-not $Path -or -not (Test-Path $Path)) { return "" }
    for ($i = 0; $i -lt 5; $i++) {
        try { return (Get-Content -LiteralPath $Path -Raw) } catch { Start-Sleep -Milliseconds 200 }
    }
    return ""
}

# The run writes several rotated logs, so "has the song started" has to count
# markers across all of them, oldest file first.
function Get-LogTextAll {
    $all = Get-LogFiles
    $text = ""
    for ($i = $all.Count - 1; $i -ge 0; $i--) { $text += (Get-LogText $all[$i].FullName) }
    return $text
}

function Get-LogMarkerTimes([string]$Text, [string]$Pattern) {
    $times = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches($Text, "\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\].*?$Pattern")) {
        $times.Add([datetime]::ParseExact($m.Groups[1].Value, "yyyy-MM-dd HH:mm:ss.fff", $null)) | Out-Null
    }
    return $times.ToArray()
}

# The title opens and closes playback envelopes: the song list opens one per
# preview and closes it again within seconds, while the song's envelope stays
# open. Replaying the markers in time order therefore identifies the song, where
# counting markers does not -- a preview leaves the counts even, and a single
# stale marker from an earlier run would skew them.
function Get-PlaybackEnvelope([string]$Text) {
    $markers = New-Object System.Collections.ArrayList
    foreach ($t in Get-LogMarkerTimes $Text $startMarker) {
        $markers.Add([pscustomobject]@{ Time = $t; Kind = 'start' }) | Out-Null
    }
    foreach ($t in Get-LogMarkerTimes $Text $stopMarker) {
        $markers.Add([pscustomobject]@{ Time = $t; Kind = 'stop' }) | Out-Null
    }
    $open = $null; $lastStop = $null; $previews = 0
    foreach ($e in @($markers | Sort-Object Time)) {
        if ($e.Kind -eq 'start') {
            if ($null -ne $open) { $previews++ }
            $open = $e.Time
        } else {
            if ($null -ne $open) { $previews++ }
            $open = $null
            $lastStop = $e.Time
        }
    }
    return [ordered]@{ Open = $open; LastStop = $lastStop; PreviewCount = $previews }
}

function Wait-ForWindow($Proc, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try {
            $Proc.Refresh()
            if ($Proc.HasExited) { return $false }
            if ($Proc.MainWindowHandle -ne [IntPtr]::Zero) { return $true }
        } catch { }
        Start-Sleep -Seconds 2
    }
    return $false
}

function Capture-To([string]$Path) {
    for ($i = 1; $i -le 3; $i++) {
        try {
            Invoke-ChildScript "capture_window.ps1" @{ OutFile = $Path } | Out-Null
            return
        } catch {
            if ($i -eq 3) { throw }
            Start-Sleep -Seconds 2
        }
    }
}

function Save-Shot([string]$Name) {
    $path = Join-Path $capDir $Name
    Capture-To $path
    return $path
}

function Get-ScreenText([string]$Shot) {
    return ((Invoke-ChildScript "ocr_image.ps1" @{ Path = $Shot }) -join " ").ToUpperInvariant()
}

function Wait-ForScreen([string]$Shot, [string]$Needle, [int]$TimeoutSec, [string]$Label) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = ""
    while ((Get-Date) -lt $deadline) {
        try {
            Capture-To $Shot
            $last = Get-ScreenText $Shot
        } catch {
            Start-Sleep -Seconds 3
            continue
        }
        if ($Needle -eq "" -or $last.Contains($Needle.ToUpperInvariant())) { return $last }
        Start-Sleep -Seconds 3
    }
    Write-Host "  !! timed out after ${TimeoutSec}s waiting for '$Needle' ($Label)"
    return $last
}

function Invoke-Actions([string[]]$Actions) {
    Invoke-ChildScript "drive_ui.ps1" @{ Actions = ($Actions -join ","); BuildDir = $BuildDir; OutDir = $OutDir } | Out-Null
}

# ------------------------------------------------------- input injection ---

function Get-GameWindow($Proc) {
    $Proc.Refresh()
    if ($Proc.HasExited) { throw "rb_blitz exited" }
    return $Proc.MainWindowHandle
}

function Focus-GameWindow($Proc) {
    $h = Get-GameWindow $Proc
    if ([RbPaceWin32]::IsIconic($h)) { [RbPaceWin32]::ShowWindow($h, 9) | Out-Null }
    for ($i = 0; $i -lt 25; $i++) {
        if ([RbPaceWin32]::GetForegroundWindow() -eq $h) {
            Start-Sleep -Milliseconds 120
            return $h
        }
        [RbPaceWin32]::keybd_event(0x12, 0x38, 0, [IntPtr]::Zero)
        [RbPaceWin32]::keybd_event(0x12, 0x38, 2, [IntPtr]::Zero)
        [RbPaceWin32]::BringWindowToTop($h) | Out-Null
        [RbPaceWin32]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 150
    }
    throw "could not foreground the rb_blitz window"
}

# One timed probe: key down at a known wall-clock instant, held for the
# requested time, key up. The timestamps are what the analyzer needs, so they
# are taken in this process rather than in a child PowerShell (whose startup
# would swamp a 8 ms hold).
#
# The key events only reach the guest while the game window holds the
# foreground: the SDK's keyboard emulation builds the pad state from that
# window's key events and drops it when the window loses focus, so a press
# stolen by another window is invisible to the guest no matter how well it
# polls. The foreground is therefore sampled for the whole hold, and a press
# that was not delivered cleanly is repeated rather than recorded as dropped
# input.
function Send-Probe($Proc, [int]$Index, [int]$HoldMs) {
    for ($attempt = 1; $attempt -le $ProbeAttempts; $attempt++) {
        $h = Get-GameWindow $Proc
        $foreground = ([RbPaceWin32]::GetForegroundWindow() -eq $h)
        $refocused = $false
        if (-not $foreground) {
            Focus-GameWindow $Proc | Out-Null
            $refocused = $true
        }
        $lost = 0
        $down = Get-Date
        [RbPaceWin32]::keybd_event([byte]$probeVk, [byte]$probeScan, 0, [IntPtr]::Zero)
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalMilliseconds -lt $HoldMs) {
            if ([RbPaceWin32]::GetForegroundWindow() -ne $h) { $lost++ }
        }
        $sw.Stop()
        [RbPaceWin32]::keybd_event([byte]$probeVk, [byte]$probeScan, 2, [IntPtr]::Zero)
        $up = Get-Date
        $stable = ($lost -eq 0 -and [RbPaceWin32]::GetForegroundWindow() -eq $h)
        if ($stable -or $attempt -eq $ProbeAttempts) {
            return [ordered]@{
                Index = $Index
                Vk = $probeVk
                Button = "A"
                ButtonBit = ("0x{0:X4}" -f $probeBit)
                RequestedHoldMs = $HoldMs
                ActualHoldMs = [int](($up - $down).TotalMilliseconds)
                DownLocal = $down.ToString("yyyy-MM-dd HH:mm:ss.fff")
                UpLocal = $up.ToString("yyyy-MM-dd HH:mm:ss.fff")
                ForegroundBefore = $foreground
                Refocused = $refocused
                Attempts = $attempt
                ForegroundStable = $stable
                ForegroundLostSamples = $lost
            }
        }
        Write-Host ("  probe {0}: the game window lost the foreground during the {1} ms hold; retrying" -f $Index, $HoldMs)
        Start-Sleep -Milliseconds 600
    }
}

# ------------------------------------------------------------------ route ---

function Select-Song([int]$Index, [string]$TagName) {
    $actions = @("key:lstick_up", "key:lstick_up", "key:lstick_up", "key:lstick_up", "key:lstick_up", "key:lstick_up")
    for ($i = 1; $i -lt $Index; $i++) { $actions += "key:lstick_down" }
    $actions += @("wait:2")
    Invoke-Actions $actions
    Save-Shot "$TagName-list.png" | Out-Null
    Invoke-Actions @("key:a")
}

# Waits for the playback envelope the title opens for a *song* rather than for
# the song list's preview. A preview closes the controller about 8 s after
# opening it, a song keeps it open: an envelope still open after $StableSec has
# to be the song.
function Wait-ForSongPlayback([int]$StableSec, [int]$TimeoutSec, [string]$Label) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $e = Get-PlaybackEnvelope (Get-LogTextAll)
        if ($null -ne $e.Open -and ((Get-Date) - $e.Open).TotalSeconds -ge $StableSec) { return $e }
        Start-Sleep -Seconds 2
    }
    Write-Host "  !! timed out after ${TimeoutSec}s waiting for $Label"
    return $null
}

# ------------------------------------------------------------------- main ---

Get-LogFiles | Remove-Item -Force
# `vsync` is a bool cvar, and the traces are noisy-gated (`log_noisy`), so both
# have to be set on the command line rather than left to the local toml.
$vsyncValue = if ($Vsync -eq "on") { "true" } else { "false" }
$exeArgs = @(
    "--game_data_root=$GameRoot",
    "--ultimate_mode=0",
    "--vsync=$vsyncValue",
    "--log_level=$LogLevel",
    "--log_noisy=true"
)

$notes = New-Object System.Collections.ArrayList
$injections = New-Object System.Collections.ArrayList
$result = [ordered]@{
    Tag = $Tag; Vsync = $Vsync; LogLevel = $LogLevel; Song = $Song; SongName = $SongName
    Pilot = [bool]$Pilot; ProbeCount = $ProbeCount; ProbeIntervalSec = $ProbeIntervalSec
    ProbeHoldsMs = ($probeHoldsMs -join ","); Exe = $exe; ExeArgs = ($exeArgs -join " ")
    Boot = $false; SongList = $false; Gameplay = $false; SongEnd = $false; Clean = $false; Fatal = $false
    ProcessStartLocal = ""; ProcessExitLocal = ""; WindowReadyLocal = ""
    GameplayStartLocal = ""; FirstProbeLocal = ""; LastProbeLocal = ""
    SongStartLocal = ""; SongStopLocal = ""; SongSeconds = 0
    LogFiles = @(); LogBytes = 0; Notes = ""
}

$p = $null
try {
    $p = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $exeArgs
    $result.ProcessStartLocal = $p.StartTime.ToString("yyyy-MM-dd HH:mm:ss.fff")

    if (-not (Wait-ForWindow $p $BootTimeoutSec)) { $notes.Add("no game window appeared") | Out-Null }
    $result.WindowReadyLocal = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff")

    $title = Wait-ForScreen (Save-Shot "$Tag-title.png") "TO START" $BootTimeoutSec "${Tag}: title screen"
    if ($title.Contains("TO START")) { $result.Boot = $true } else { $notes.Add("title screen not recognised") | Out-Null }

    Invoke-Actions @("key:a")
    $signin = Wait-ForScreen (Save-Shot "$Tag-signin.png") "ROCK CENTRAL" $ScreenTimeoutSec "${Tag}: sign-in dialog"
    if (-not $signin.Contains("ROCK CENTRAL")) { $notes.Add("sign-in dialog not recognised") | Out-Null }
    Invoke-Actions @("key:a")
    $offline = Wait-ForScreen (Save-Shot "$Tag-offline.png") "OFFLINE MODE" $ScreenTimeoutSec "${Tag}: offline prompt"
    if (-not $offline.Contains("OFFLINE MODE")) { $notes.Add("offline prompt not recognised") | Out-Null }
    Invoke-Actions @("key:a")
    $menu = Wait-ForScreen (Save-Shot "$Tag-menu.png") "PLAY" $ScreenTimeoutSec "${Tag}: main menu"
    if (-not $menu.Contains("PLAY")) { $notes.Add("main menu not recognised") | Out-Null }
    Invoke-Actions @("key:a")
    $list = Wait-ForScreen (Save-Shot "$Tag-songs.png") "YOUR SONGS" $ScreenTimeoutSec "${Tag}: song list"
    if ($list.Contains("YOUR SONGS")) { $result.SongList = $true } else { $notes.Add("song list not recognised") | Out-Null }

    Select-Song $Song $Tag
    # The how-to-play card in front of the song is cleared with A.
    $text = Wait-ForScreen (Save-Shot "$Tag-loaded.png") "BEGIN" $ScreenTimeoutSec "${Tag}: song load card"
    if ($text.Contains("BEGIN")) { Invoke-Actions @("key:a") } else { $notes.Add("no how-to-play card seen") | Out-Null }

    # 20 s of patience: a preview cannot survive that, so the envelope that is
    # still open is the song.
    $play = Wait-ForSongPlayback 20 $SongTimeoutSec "${Tag}: song playback"
    if ($play) {
        $result.Gameplay = $true
        $result.GameplayStartLocal = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff")
        $result.SongStartLocal = $play.Open.ToString("yyyy-MM-dd HH:mm:ss.fff")
    } else {
        $notes.Add("the song never started") | Out-Null
    }

    if ($result.Gameplay) {
        # Node: the probes are the only input during the song, so this is also
        # the check that scripted input reaches the guest at all.
        Focus-GameWindow $p | Out-Null
        $probeDeadline = if ($Pilot) { (Get-Date).AddSeconds($PilotSeconds) } else { (Get-Date).AddSeconds(1e9) }
        for ($i = 1; $i -le $ProbeCount; $i++) {
            if ((Get-Date) -ge $probeDeadline) { break }
            $hold = $probeHoldsMs[($i - 1) % $probeHoldsMs.Count]
            $inj = Send-Probe $p $i $hold
            $injections.Add([pscustomobject]$inj) | Out-Null
            if ($result.FirstProbeLocal -eq "") { $result.FirstProbeLocal = $inj.DownLocal }
            $result.LastProbeLocal = $inj.DownLocal
            Write-Host ("  probe {0}: hold {1} ms ({2} ms measured)" -f $i, $hold, $inj.ActualHoldMs)
            # Keep clear of the next probe's slot, and stop probing if the song
            # already ended (the results screen would read the presses).
            $wait = [Math]::Min($ProbeIntervalSec, [Math]::Max(1, ($probeDeadline - (Get-Date)).TotalSeconds))
            if (-not $Pilot) {
                $wait = [Math]::Min($wait, $ProbeIntervalSec)
            }
            Start-Sleep -Seconds ([int]$wait)
            if (-not $Pilot) {
                $e = Get-PlaybackEnvelope (Get-LogTextAll)
                if ($null -eq $e.Open -and $null -ne $e.LastStop -and $e.LastStop -gt $play.Open) {
                    $notes.Add("the song ended after probe $i") | Out-Null
                    break
                }
            }
        }
    }

    # Then play the song out (unless the pilot stops here) so the analyzer has
    # the song's own end marker to bound the gameplay window with.
    if ($result.Gameplay -and -not $Pilot) {
        $deadline = (Get-Date).AddSeconds($SongTimeoutSec)
        $stop = $null
        while ((Get-Date) -lt $deadline) {
            $e = Get-PlaybackEnvelope (Get-LogTextAll)
            if ($null -ne $e.LastStop -and $e.LastStop -gt $play.Open) { $stop = $e.LastStop; break }
            Start-Sleep -Seconds 3
        }
        if ($stop) {
            $result.SongEnd = $true
            $result.SongStopLocal = $stop.ToString("yyyy-MM-dd HH:mm:ss.fff")
            $result.SongSeconds = [int]($stop - $play.Open).TotalSeconds
        } else {
            $notes.Add("the song never reached its stop marker") | Out-Null
        }
        Start-Sleep -Seconds 8
        Save-Shot "$Tag-results.png" | Out-Null
    }

    if (-not $p.HasExited) {
        $p.CloseMainWindow() | Out-Null
        $result.Clean = $p.WaitForExit($CloseWaitSec * 1000)
        if (-not $result.Clean) { $notes.Add("window close did not exit the process") | Out-Null }
    } else {
        $notes.Add("process exited on its own (exit code $($p.ExitCode))") | Out-Null
    }
} finally {
    if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    $result.ProcessExitLocal = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff")
}

# ------------------------------------------------------------- collect ---

# The rotated files have to be reassembled in write order; sorting by the first
# timestamp in each file does that without depending on the rotation naming.
$ordered = New-Object System.Collections.ArrayList
foreach ($f in (Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue)) {
    $first = ""
    try {
        $sr = [IO.File]::OpenText($f.FullName)
        try { $first = $sr.ReadLine() } finally { $sr.Close() }
    } catch { }
    $ordered.Add([pscustomobject]@{ File = $f; First = $first }) | Out-Null
}
$sorted = @($ordered | Sort-Object First)
$k = 0
foreach ($o in $sorted) {
    $k++
    $name = ("log{0:D2}-{1}" -f $k, $o.File.Name)
    Copy-Item $o.File.FullName (Join-Path $logDir $name) -Force
    $result.LogFiles += $name
    $result.LogBytes += $o.File.Length
}
if (-not $sorted) { $notes.Add("no log file was produced") | Out-Null }

$full = Get-LogTextAll
$result.Fatal = [bool]($full | Select-String -Pattern "\[FATAL\]" -Quiet)
if ($result.Fatal) { $notes.Add("log contains [FATAL]") | Out-Null }

$injections | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $capDir "injections.json")
$result.Notes = ($notes -join "; ")
[pscustomobject]$result | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $capDir "run.json")

Write-Host "`n=== measurement run: $Tag ==="
[pscustomobject]$result | Format-List Tag, Vsync, LogLevel, Boot, SongList, Gameplay, SongEnd, Clean, Fatal, SongSeconds, LogBytes | Out-String | Write-Host
Write-Host "probes injected: $($injections.Count) -> $capDir\injections.json"
if ($result.Notes) { Write-Host "notes: $($result.Notes)" }
Write-Host "logs: $($result.LogFiles -join ', ')"

# The route has to have happened, otherwise the evidence is about the wrong
# screen. The pacing verdict itself is the analyzer's job.
if (-not ($result.Boot -and $result.SongList -and $result.Gameplay -and $result.Clean -and -not $result.Fatal)) { exit 1 }
exit 0
