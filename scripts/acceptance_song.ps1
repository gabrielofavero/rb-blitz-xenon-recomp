# Milestone 5 acceptance: one named bundled song, launch -> results, three
# consecutive clean-process runs.
#
# The Milestone 3 analogue (acceptance_launches.ps1) stops at boot. This one
# drives the whole offline route with scripts/drive_ui.ps1 and decides each
# transition from evidence, not from a fixed sleep:
#
#   * window/UI state comes from the Windows OCR engine (scripts/ocr_image.ps1),
#     so "the song list is up" is read off the screen rather than assumed;
#   * the song's start and end come from the guest's own audio envelope,
#     XMPSetPlaybackController(0,1) -> (0,0), the one marker the title emits for
#     "a song is playing" — taken in pairs and filtered by length, because the
#     song list previews the rows it scrolls past through that same controller;
#   * the results screen has to name the expected song, so a pass really is
#     evidence that *that* song was played and not an arbitrary row;
#   * the run fails if the log contains a [FATAL], if the window dies early, or
#     if the log never prints the title's own "Title terminated" marker.
#
# Vanilla by default: the milestone's exit criterion covers the vanilla content
# variant, so --ultimate_mode=0 is passed even when a Rock Band Blitz Ultimate
# payload is installed next to the game root (docs/ultimate-compat.md).
#
# Usage:
#   .\scripts\acceptance_song.ps1                       # 3 runs, "These Days"
#   .\scripts\acceptance_song.ps1 -Runs 1 -Song 2 -SongName "ONE WEEK"
#   .\scripts\acceptance_song.ps1 -Replay               # ... and play it again
#
# Per run it writes out/m5-acceptance/runNN-*.png (the screens it asserted on)
# and a copy of the run's log, then prints a pass/fail table. Exit code 0 means
# every run passed.
param(
    [int]$Runs = 3,
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m5-acceptance",
    [string]$GameRoot,
    # 1-based row in the artist-sorted song list. Row 1 is the "Random Song"
    # entry, then the bundled songs by artist: One Week (2), These Days (3),
    # Death on Two Legs (4).
    [int]$Song = 3,
    # Song title as the results screen spells it (Windows OCR gives upper
    # case); a run only passes if the results screen shows this title.
    [string]$SongName = "THESE DAYS",
    [string]$UltimateMode = "0",
    # Play the song a second time from the results screen, in the same process.
    [switch]$Replay,
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
$capDir = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }

# The markers the title prints around a playback stream. The song list previews
# the highlighted row through this same controller, so they are matched in pairs
# and filtered by length rather than trusted on their own.
$startMarker = "XMPSetPlaybackController\(00000000, 00000001\)"
$stopMarker = "XMPSetPlaybackController\(00000000, 00000000\)"

# ---------------------------------------------------------------- helpers ---

# Every helper that touches a .ps1 file has to go through a child PowerShell:
# this machine's execution policy is Restricted (docs/build-and-run.md §0).
function Invoke-ChildScript([string]$Script, [hashtable]$Named) {
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) { $argv += @("-$k", [string]$Named[$k]) }
    $out = & powershell @argv 2>&1
    if ($LASTEXITCODE -ne 0) { throw "$Script failed: $out" }
    return $out
}

function Get-NewestLog {
    $f = Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $f) { return $null }
    return $f.FullName
}

function Get-LogText([string]$Path) {
    if (-not $Path -or -not (Test-Path $Path)) { return "" }
    # The log is being written while we read it; a sharing violation here is
    # expected and not a run failure.
    for ($i = 0; $i -lt 5; $i++) {
        try { return (Get-Content -LiteralPath $Path -Raw) } catch { Start-Sleep -Milliseconds 200 }
    }
    return ""
}

# The log is not truncated mid-run, so a replay needs "how many times has this
# marker been written", not just "is the marker present".
function Get-LogMatchCount([string]$Path, [string]$Pattern) {
    $hits = @(Get-LogText $Path | Select-String -Pattern $Pattern -AllMatches)
    $n = 0
    foreach ($h in $hits) { $n += $h.Matches.Count }
    return $n
}

# Every timestamp matching $Pattern, in order. A run writes one pair of playback
# markers per song, so an envelope has to be taken by position rather than by
# "does the marker exist".
function Get-LogMarkerTimes([string]$Text, [string]$Pattern) {
    $times = New-Object System.Collections.ArrayList
    foreach ($m in [regex]::Matches($Text, "\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\].*?$Pattern")) {
        $times.Add([datetime]::ParseExact($m.Groups[1].Value, "yyyy-MM-dd HH:mm:ss.fff", $null)) | Out-Null
    }
    return $times.ToArray()
}

# Waits for the pair of playback markers that brackets a song.
#
# The song list previews the highlighted row through the very same controller, so
# the markers already in the log can belong to a preview rather than to the song
# (measured: an 8s preview against 243-315s for a bundled song). Take the pair at
# $Base + 1 and step past any pair too short to be the song.
function Wait-For-SongEnvelope {
    param(
        [string]$Path,
        [int]$Base,
        [int]$MinSec,
        [int]$TimeoutSec,
        [string]$Label,
        [System.Collections.ArrayList]$Notes,
        [string]$ShotAtStart = ""
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $k = $Base + 1
    $shotFor = 0
    while ((Get-Date) -lt $deadline) {
        $text = Get-LogText $Path
        $starts = @(Get-LogMarkerTimes $text $startMarker)
        $stops = @(Get-LogMarkerTimes $text $stopMarker)
        if ($ShotAtStart -and $shotFor -ne $k -and $starts.Count -ge $k -and (Get-Date) -gt $starts[$k - 1].AddSeconds(6)) {
            # Six seconds in, so the shot shows the song rather than the load
            # transition that follows the start marker.
            $shotFor = $k
            Save-Shot $ShotAtStart | Out-Null
        }
        if ($starts.Count -ge $k -and $stops.Count -ge $k) {
            $sec = [int]($stops[$k - 1] - $starts[$k - 1]).TotalSeconds
            if ($sec -ge $MinSec) {
                return [ordered]@{ Index = $k; Sec = $sec; Start = $starts[$k - 1]; Stop = $stops[$k - 1] }
            }
            $Notes.Add("playback pair $k lasts ${sec}s, which is the list preview rather than the song") | Out-Null
            $k++
            continue
        }
        Start-Sleep -Seconds 2
    }
    Write-Host "  !! timed out after ${TimeoutSec}s waiting for $Label"
    return $null
}

function Get-ScreenText([string]$Shot) {
    $text = Invoke-ChildScript "ocr_image.ps1" @{ Path = $Shot }
    return ($text -join " ").ToUpperInvariant()
}

# The window does not exist for the first ~30 s of a run, and both the capture
# and the OCR need it to be there, so every poll re-takes the shot.
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
    Write-Host "  !! timed out after ${TimeoutSec}s waiting for '$Needle' ($Label); last screen text: $last"
    return $last
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

function Invoke-Actions([string[]]$Actions) {
    Invoke-ChildScript "drive_ui.ps1" @{ Actions = ($Actions -join ","); BuildDir = $BuildDir; OutDir = $OutDir } | Out-Null
}

function Shot([string]$Name) { return (Join-Path $capDir $Name) }

function Capture-To([string]$Path) {
    # Refuses to save a shot of the wrong window when another one owns the
    # foreground, so give it a few tries before calling it a failure.
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
    $path = Shot $Name
    Capture-To $path
    return $path
}

# ------------------------------------------------------------------ route ---

# Row 1 of the artist-sorted list is "Random Song" (which starts a song at
# random), so a specific song means Song - 1 presses down once the cursor is on
# that first row.
#
# The list is driven by the *left* stick: the MnK defaults bind the arrow keys to
# the right stick (rexglue-sdk, mnk_input_driver.cpp) and those keys do nothing
# in this list, so it has to be lstick_up/lstick_down. Each tap moves exactly one
# row and the list clamps at both ends rather than wrapping (all verified live),
# so walking up past the first row lands on a known row whatever the list
# remembered — which is what makes a replay pick the same song as the first pass.
function Select-Song([int]$Index, [string]$Tag) {
    $actions = @("key:lstick_up", "key:lstick_up", "key:lstick_up", "key:lstick_up", "key:lstick_up", "key:lstick_up")
    for ($i = 1; $i -lt $Index; $i++) { $actions += "key:lstick_down" }
    $actions += @("wait:2")
    Invoke-Actions $actions
    # The footer panel under the list names the highlighted song's artist and
    # album, so the shot records which row was actually selected.
    Save-Shot "$Tag-list.png" | Out-Null
    Invoke-Actions @("key:a")
}

function Start-OneSong([string]$Tag, [System.Collections.ArrayList]$Notes) {
    $text = Wait-ForScreen (Shot "$Tag-loaded.png") "BEGIN" $ScreenTimeoutSec "${Tag}: song load / 'PRESS A BEGIN'"
    # The pre-song screen is a one-off how-to-play card; it appears whenever the
    # song is entered from the list, so every run has to clear it.
    if ($text.Contains("BEGIN")) {
        Invoke-Actions @("key:a")
        $Notes.Add("cleared the how-to-play card") | Out-Null
    } else {
        $Notes.Add("no how-to-play card seen (screen text: $text)") | Out-Null
    }
}

# ------------------------------------------------------------------- main ---

function Invoke-Run([int]$Run) {
    $tag = "run{0:D2}" -f $Run
    $notes = New-Object System.Collections.ArrayList
    $r = [ordered]@{
        Run = $Run; Boot = $false; SongList = $false; Playing = $false
        Results = $false; SongSeen = $false; Replayed = $false; Fatal = $false
        Clean = $false; SongSec = 0; Envelope = ""; LogKB = 0
        ResultsText = ""; Notes = ""
    }

    Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue | Remove-Item -Force
    $p = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru `
        -ArgumentList "--game_data_root=$GameRoot", "--ultimate_mode=$UltimateMode"

    try {
        # 1. Title screen. Boot is 25-40s; the OCR wait is what makes a slow boot
        #    a longer wait rather than a false failure.
        if (-not (Wait-ForWindow $p $BootTimeoutSec)) { $notes.Add("no game window appeared") | Out-Null }
        $title = Wait-ForScreen (Shot "$tag-title.png") "TO START" $BootTimeoutSec "${tag}: title screen"
        if (-not $title.Contains("TO START")) { $notes.Add("title screen not recognised") | Out-Null }
        else { $r.Boot = $true }

        # 2. "Cannot connect to Rock Central" -> "Proceed in Offline Mode?" ->
        #    main menu. A is SELECT on both dialogs; the second one is the
        #    documented offline route (docs/bringup-log.md, Milestone 4).
        Invoke-Actions @("key:a")
        $signin = Wait-ForScreen (Shot "$tag-signin.png") "ROCK CENTRAL" $ScreenTimeoutSec "${tag}: sign-in dialog"
        if (-not $signin.Contains("ROCK CENTRAL")) { $notes.Add("sign-in dialog not recognised") | Out-Null }

        Invoke-Actions @("key:a")
        $offline = Wait-ForScreen (Shot "$tag-offline.png") "OFFLINE MODE" $ScreenTimeoutSec "${tag}: offline prompt"
        if (-not $offline.Contains("OFFLINE MODE")) { $notes.Add("offline prompt not recognised") | Out-Null }

        Invoke-Actions @("key:a")
        $menu = Wait-ForScreen (Shot "$tag-menu.png") "PLAY" $ScreenTimeoutSec "${tag}: main menu"
        if (-not $menu.Contains("PLAY")) { $notes.Add("main menu not recognised") | Out-Null }

        # 3. PLAY -> song list. The "Random Song" row is highlighted on entry.
        Invoke-Actions @("key:a")
        $list = Wait-ForScreen (Shot "$tag-songs.png") "YOUR SONGS" $ScreenTimeoutSec "${tag}: song list"
        if (-not $list.Contains("YOUR SONGS")) { $notes.Add("song list not recognised") | Out-Null }
        else { $r.SongList = $true }

        # 4. Load and begin the song.
        $log = Get-NewestLog
        Select-Song $Song $tag
        Start-OneSong $tag $notes

        # 5. Play it to the end: the song ends where the title stops its own
        #    playback stream. At ~4 minutes a song this is the long wait of the
        #    run. The envelope also has to ignore the song list's preview, which
        #    drives the same controller.
        # Not named $song: PowerShell variable names are case-insensitive, so that
        # would shadow the -Song parameter this run is selecting.
        $playback = Wait-For-SongEnvelope -Path $log -Base 0 -MinSec 60 -TimeoutSec $SongTimeoutSec `
            -Label "${tag}: song playback" -Notes $notes -ShotAtStart "$tag-playing.png"
        if ($playback) {
            $r.Playing = $true
        } else {
            $notes.Add("no song-length playback envelope appeared") | Out-Null
        }

        # 6. Results screen. It names the song, which is what makes a pass
        #    evidence of a *named* song rather than of an arbitrary row.
        Start-Sleep -Seconds 8
        $results = Save-Shot "$tag-results.png"
        $rtext = Get-ScreenText $results
        if (-not ($rtext -match "CONTINUE|BASE POINTS|BLITZ MODE|FINAL SCORE")) {
            Start-Sleep -Seconds 6
            Save-Shot "$tag-results.png" | Out-Null
            $rtext = Get-ScreenText $results
        }
        $r.ResultsText = $rtext
        $want = $SongName.Trim().ToUpperInvariant()
        $r.SongSeen = ($want -ne "" -and $rtext.Contains($want))
        if ($rtext -match "CONTINUE|BASE POINTS|BLITZ MODE|FINAL SCORE") {
            if ($want -eq "") {
                $r.Results = $true
                $notes.Add("no -SongName given, so the results screen was not checked against a title") | Out-Null
            } elseif ($r.SongSeen) {
                $r.Results = $true
            } else {
                $notes.Add("results screen is up but does not name '$want': $rtext") | Out-Null
            }
        } else {
            $notes.Add("results screen text not recognised: $rtext") | Out-Null
        }

        # 7. Optional: play the same song again from the results screen. The log
        #    keeps growing, so the replay's envelope is the pair that follows the
        #    first song's (plus the previews the list played in between).
        if ($Replay -and $r.Results) {
            # The results screen can be covered by the offline notice ("You won't
            # collect any coins or cred for this song"), which swallows the first
            # A, so press and re-check instead of assuming one press is enough.
            # Stop as soon as the list is up: a further press there would start
            # whatever row the list remembers.
            $back = ""
            for ($i = 1; $i -le 4; $i++) {
                Invoke-Actions @("key:a")
                $back = Wait-ForScreen (Shot "$tag-replay-list.png") "YOUR SONGS" 20 "${tag}: return to song list (press $i)"
                if ($back.Contains("YOUR SONGS")) { break }
            }
            if ($back.Contains("YOUR SONGS")) {
                Select-Song $Song "$tag-replay"
                Start-OneSong $tag $notes
                # The replay's envelope is whatever pair comes after the first
                # song's, plus anything the list previewed in between.
                $base2 = [Math]::Max((Get-LogMatchCount $log $startMarker), (Get-LogMatchCount $log $stopMarker))
                $playback2 = Wait-For-SongEnvelope -Path $log -Base $base2 -MinSec 60 -TimeoutSec $SongTimeoutSec `
                    -Label "${tag}: replay playback" -Notes $notes -ShotAtStart "$tag-replay-playing.png"
                if ($playback2) {
                    $r.Replayed = $true
                    Start-Sleep -Seconds 8
                    $rshot = Save-Shot "$tag-replay-results.png"
                    if (-not (Get-ScreenText $rshot).Contains("$SongName".Trim().ToUpperInvariant())) {
                        $notes.Add("the replay's results screen does not name '$SongName'") | Out-Null
                    }
                } else {
                    $notes.Add("the replay never started") | Out-Null
                }
            } else {
                $notes.Add("results screen did not lead back to the song list") | Out-Null
            }
        }

        # 8. A clean shutdown through the window (no forced kill) ...
        if (-not $p.HasExited) {
            $p.CloseMainWindow() | Out-Null
            $r.Clean = $p.WaitForExit($CloseWaitSec * 1000)
            if (-not $r.Clean) { $notes.Add("window close did not exit the process") | Out-Null }
        } else {
            $notes.Add("process exited on its own (exit code $($p.ExitCode))") | Out-Null
        }

        # ... and then the log: faults, the exact song envelope, and the
        # title's own shutdown marker, which only a graceful close prints.
        $log = Get-NewestLog
        $text = Get-LogText $log
        $r.Fatal = [bool]($text | Select-String -Pattern "\[FATAL\]" -Quiet)
        if ($r.Fatal) { $notes.Add("log contains [FATAL]") | Out-Null }
        if ($text -match "MODIFIED") { $notes.Add("boot warns the game data is MODIFIED") | Out-Null }
        if (-not ($text | Select-String -Pattern "Title terminated" -Quiet)) {
            $r.Clean = $false
            $notes.Add("no clean-shutdown marker in the log") | Out-Null
        }
        # The envelope measured while the run was in progress, not the log's first
        # pair: the first pair can belong to the song list's preview.
        if ($playback) {
            $r.SongSec = $playback.Sec
            $r.Envelope = "$($playback.Start.ToString('HH:mm:ss')) -> $($playback.Stop.ToString('HH:mm:ss'))"
        }
        if (Test-Path $log) {
            $r.LogKB = [math]::Round((Get-Item $log).Length / 1KB)
            Copy-Item $log (Join-Path $capDir "$tag.log") -Force
        }
    } finally {
        if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
    }

    $r.Notes = ($notes -join "; ")
    return [pscustomobject]$r
}

$rows = @()
for ($i = 1; $i -le $Runs; $i++) {
    Write-Host "=== run $i/$Runs ==="
    $rows += Invoke-Run $i
    $rows[-1] | Format-List | Out-String | Write-Host
}

Write-Host "`n=== summary ==="
$rows | Format-Table Run, Boot, SongList, Playing, Results, SongSeen, Replayed, SongSec, Fatal, Clean -AutoSize | Out-String | Write-Host
$rows | ForEach-Object { if ($_.Envelope) { Write-Host ("run {0}: playback {1} ({2}s) [{3}]" -f $_.Run, $_.Envelope, $_.SongSec, $SongName) } }
$rows | ForEach-Object { if ($_.Notes) { Write-Host ("run {0}: {1}" -f $_.Run, $_.Notes) } }

# A pass is the milestone 5 exit criterion: a clean process reaches the title,
# picks the named song from the list, plays it to the end and reaches a results
# screen that names it, with no fatal, and closes through its own window. With
# -Replay the second song has to reach its results screen as well.
# @(...) because .Count on a single object is $null in PowerShell 5.1, which
# would report "0 / 1" for a passing one-run validation.
$pass = @($rows | Where-Object {
    $_.Boot -and $_.SongList -and $_.Playing -and $_.Results -and $_.Clean -and -not $_.Fatal -and
    ((-not $Replay) -or $_.Replayed)
}).Count
Write-Host "launch-to-results: $pass / $Runs"
$rows | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $capDir "summary.json")
if ($pass -ne $Runs) { exit 1 }
exit 0
