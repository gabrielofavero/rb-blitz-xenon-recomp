# DLC acceptance: downloaded content is mounted and its songs appear in the
# game's own song-selection screen.
#
# docs/dlc.md §5 left this open: the layout and the patch set are pinned host-side
# (tests/dlc_layout_tests.cpp, patches/rexglue-sdk/0006-...), but "the guest's own
# song list is the only thing that proves the enumeration and the mount". This is
# that proof, and it runs the two halves docs/dlc.md §5 names:
#
#   * `dlc: N package(s) for title(s) ... (read-only, mounted in place)` - the host
#     hook found and registered the packages; a flat library reports the same shape
#     of line, so the leg's package count is the sum of either source;
#   * `XamContentAggregateCreateEnumerator: added M items` - the guest's own
#     enumeration returned them (M > 0), where the count is 0 with no packages;
#   * and the song list itself, read with Windows OCR, naming a song that only the
#     DLC package carries.
#
# A control leg on the same executable with `--dlc_root` pointed at a path that
# does not exist is what makes the positive leg mean something: the DLC song has to
# be *absent* there, or the song came from the bundled game data and not from the
# DLC root. Both legs use their own fresh --user_data_root, because the guest
# caches its song list into `songcache` and a reused root could serve a cached DLC
# song to the control leg.
#
# -DlcLibrary adds the flat library half of docs/dlc.md §2.1 to the positive leg
# (`--dlc_library`), with the control leg pointed at a library path that does not
# exist as well, so the same two legs prove either source.
#
# The expected song is configuration, not a constant of the format: this dump's
# `game/dlc/45410914/00000002/<package>` is the "Rock Band Blitz Soundtrack"
# (content type 0x00000002, title 45410914), whose first artist-sorted row is
# All-American Rejects' "Kids in the Street". A different dump needs -ExpectedSong.
# The 5841122D/00009000 packages in this dump are avatar items (T-shirts, jumpsuits,
# sunglasses), not songs, and are intentionally not expected here.
#
# Usage:
#   .\scripts\acceptance_dlc.ps1
#   .\scripts\acceptance_dlc.ps1 -ExpectedSong "KIDS IN THE STREET,SO FAR AWAY"
#   .\scripts\acceptance_dlc.ps1 -DlcLibrary "D:\Games\YARG Songs" -ExpectedSong "HERE WITHOUT YOU"
#   .\scripts\acceptance_dlc.ps1 -SkipControl
#
# Writes out/m7-dlc/<leg>-*.png, a copy of each run's log and summary.json, prints
# a per-leg verdict and exits non-zero unless every leg held.
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m7-dlc",
    [string]$GameRoot,
    # Root of the <title_id>/<content_type>/<package> layout. Empty means the
    # default, <GameRoot>/dlc (docs/dlc.md §2).
    [string]$DlcRoot,
    # A ';'-separated flat DLC library: folders of STFS packages in any arrangement,
    # each mounted under the title id and content type its own header names
    # (docs/dlc.md §2.1). Empty means no library, which is the pre-library behaviour.
    [string]$DlcLibrary = "",
    # One or more song titles, comma-separated, that only the DLC package carries.
    [string]$ExpectedSong = "KIDS IN THE STREET",
    [string]$UltimateMode = "0",
    [int]$BootTimeoutSec = 180,
    [int]$ScreenTimeoutSec = 90,
    [int]$CloseWaitSec = 20,
    [switch]$SkipControl
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
# -BuildDir may be absolute (an installed folder outside the repo) or relative to
# the repository root; Join-Path does not resolve an absolute child, so test first.
$work = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$exe = Join-Path $work "rb_blitz.exe"
$logs = Join-Path $work "logs"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
if (-not $DlcRoot) { $DlcRoot = Join-Path $GameRoot "dlc" }
$capDir = if ([System.IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }
if (-not (Test-Path $DlcRoot)) { throw "no DLC root at $DlcRoot - nothing for the positive leg to load" }
foreach ($lib in ($DlcLibrary -split ";" | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
    if (-not (Test-Path $lib)) { throw "no DLC library at $lib" }
}

$expected = @($ExpectedSong.Split(",") | ForEach-Object { $_.Trim().ToUpperInvariant() } | Where-Object { $_ })
if ($expected.Count -eq 0) { throw "-ExpectedSong named no song" }

# ---------------------------------------------------------------- helpers ---

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
    for ($i = 0; $i -lt 5; $i++) {
        try { return (Get-Content -LiteralPath $Path -Raw) } catch { Start-Sleep -Milliseconds 200 }
    }
    return ""
}

function Save-Shot([string]$Name) {
    $path = Join-Path $capDir $Name
    for ($i = 1; $i -le 3; $i++) {
        try { Invoke-ChildScript "capture_window.ps1" @{ OutFile = $path } | Out-Null; return $path }
        catch { if ($i -eq 3) { throw }; Start-Sleep -Seconds 2 }
    }
}

function Get-ScreenText([string]$Shot) {
    return ((Invoke-ChildScript "ocr_image.ps1" @{ Path = $Shot }) -join " ").ToUpperInvariant()
}

function Wait-ForScreen([string]$Shot, [string]$Needle, [int]$TimeoutSec, [string]$Label) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = ""
    while ((Get-Date) -lt $deadline) {
        try { Save-Shot (Split-Path -Leaf $Shot) | Out-Null; $last = Get-ScreenText $Shot }
        catch { Start-Sleep -Seconds 3; continue }
        if ($Needle -eq "" -or $last.Contains($Needle.ToUpperInvariant())) { return $last }
        Start-Sleep -Seconds 3
    }
    Write-Host "  !! timed out after ${TimeoutSec}s waiting for '$Needle' ($Label); last: $last"
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

# ------------------------------------------------------------------- legs ---

function Invoke-Leg([string]$Name, [string]$DlcRootPath, [string]$DlcLibraryPath, [int]$MinPackages, [int]$MinItems, [int]$MaxItems) {
    Write-Host "=== $Name (dlc_root=$DlcRootPath library=$DlcLibraryPath) ==="
    $legDir = Join-Path $capDir "$Name-userdata"
    if (Test-Path $legDir) { Remove-Item -Recurse -Force $legDir }
    New-Item -ItemType Directory -Force -Path $legDir | Out-Null
    Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue | Remove-Item -Force

    $notes = New-Object System.Collections.ArrayList
    $row = [ordered]@{
        Leg = $Name; DlcRoot = $DlcRootPath; DlcLibrary = $DlcLibraryPath; SongList = $false
        DlcSong = $false; Packages = 0; AggregateItems = -1; Fatal = $false
        Expected = ($expected -join " & "); FoundSongs = ""; Notes = ""
    }
    # --dlc_library is passed only when a library was named, so a leg that is not
    # about one runs with the pre-library behaviour alone. Paths are quoted: a library
    # usually has a space in it ("YARG Songs"), and Start-Process joins the array into
    # one command line without quoting on its own.
    $launchArgs = @("--game_data_root=""$GameRoot""", "--ultimate_mode=$UltimateMode",
        "--mnk_mode=1", "--no_mouse_ui_nav", "--user_data_root=""$legDir""",
        "--dlc_root=""$DlcRootPath""", "--log_level=debug", "--log_flush_interval=1",
        "--log_max_file_size_mb=200")
    if ($DlcLibraryPath) { $launchArgs += "--dlc_library=""$DlcLibraryPath""" }
    $proc = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $launchArgs
    try {
        if (-not (Wait-ForWindow $proc $BootTimeoutSec)) { $notes.Add("no game window appeared") | Out-Null }
        else {
            # Screen-driven, the way scripts/acceptance_song.ps1 walks the same route: a
            # leg with a large library spends minutes discovering content before the
            # title screen is even interactive, and fixed waits desynchronised the route
            # (measured: with D:\Games\YARG Songs the A presses landed before the
            # dialogs and the run ended on the Rock Central prompt).
            $null = Wait-ForScreen (Join-Path $capDir "$Name-title.png") "TO START" $BootTimeoutSec "${Name}: title screen"
            Invoke-Actions @("key:a")
            $null = Wait-ForScreen (Join-Path $capDir "$Name-signin.png") "ROCK CENTRAL" $ScreenTimeoutSec "${Name}: sign-in dialog"
            Invoke-Actions @("key:a")
            $null = Wait-ForScreen (Join-Path $capDir "$Name-offline.png") "OFFLINE MODE" $ScreenTimeoutSec "${Name}: offline prompt"
            Invoke-Actions @("key:a")
            $null = Wait-ForScreen (Join-Path $capDir "$Name-menu.png") "PLAY" $ScreenTimeoutSec "${Name}: main menu"
            # PLAY is the main menu's first row, so no highlight reset is needed; the
            # song list is what the whole leg waits for, and building it is what mounts
            # every package a big library carries.
            Invoke-Actions @("key:a")
            $text = Wait-ForScreen (Join-Path $capDir "$Name-songs.png") "YOUR SONGS" $ScreenTimeoutSec "${Name}: song list"
            $row.SongList = $text.Contains("YOUR SONGS")
            if (-not $row.SongList) { $notes.Add("the song list did not come up") | Out-Null }
            $found = @($expected | Where-Object { $text.Contains($_) })
            $row.FoundSongs = ($found -join ", ")
            $row.DlcSong = ($found.Count -eq $expected.Count)
            if (-not $row.DlcSong -and $MinItems -ge 1) {
                $notes.Add("of the DLC songs '$($expected -join " & ")', only [$($row.FoundSongs)] were on the list") | Out-Null
            }
        }
    } finally {
        if (-not $proc.HasExited) {
            try {
                $proc.CloseMainWindow() | Out-Null
                if (-not $proc.WaitForExit($CloseWaitSec * 1000)) { Stop-Process -Id $proc.Id -Force }
            } catch { }
        }
    }

    $log = Get-NewestLog
    $logText = Get-LogText $log
    $row.Fatal = [bool]($logText | Select-String -Pattern "\[FATAL\]" -Quiet)
    if ($row.Fatal) { $notes.Add("the log contains [FATAL]") | Out-Null }
    # Both the structured root and a library report `dlc: N package(s) ...`, so the
    # leg's count is the sum of what either source registered.
    foreach ($p in [regex]::Matches($logText, "dlc: (\d+) package\(s\)")) {
        $row.Packages += [int]$p.Groups[1].Value
    }
    foreach ($a in [regex]::Matches($logText, "XamContentAggregateCreateEnumerator: added (\d+) items")) {
        $n = [int]$a.Groups[1].Value
        if ($n -gt $row.AggregateItems) { $row.AggregateItems = $n }
    }
    if ($row.Packages -lt $MinPackages) {
        $notes.Add("the DLC hook reported $($row.Packages) package(s); the leg requires at least $MinPackages") | Out-Null
    }
    if ($row.AggregateItems -lt $MinItems -or $row.AggregateItems -gt $MaxItems) {
        $notes.Add("the guest's aggregate enumerator added at most $($row.AggregateItems) item(s); the leg requires $MinItems..$MaxItems") | Out-Null
    }
    if ($log) { Copy-Item $log (Join-Path $capDir "$Name.log") -Force }
    $row.Notes = ($notes -join "; ")
    Write-Host ("  songList={0} dlcSong={1} packages={2} aggregateItems={3} fatal={4}" -f `
        $row.SongList, $row.DlcSong, $row.Packages, $row.AggregateItems, $row.Fatal)
    return [pscustomobject]$row
}

# ------------------------------------------------------------------- main ---

# The positive leg expects the packages docs/dlc.md §2 describes. The count is not
# hard-coded: it is whatever the hook reports, as long as it is at least one, and
# the guest's enumerator agrees it got something.
$dlc = Invoke-Leg "dlc" $DlcRoot $DlcLibrary 1 1 2147483647
$dlcPass = $dlc.SongList -and $dlc.DlcSong -and ($dlc.Packages -ge 1) -and
           ($dlc.AggregateItems -ge 1) -and -not $dlc.Fatal

$controlPass = $true
$control = $null
if (-not $SkipControl) {
    # Paths that do not exist: the hook refuses both sources (docs/dlc.md §3) and
    # the guest's enumerator must come back empty, with the DLC song not on the list.
    # A library only joins the control leg when the positive leg had one, so a run
    # without -DlcLibrary keeps the original control shape.
    $absent = Join-Path $capDir "no-dlc-root"
    $absentLibrary = if ($DlcLibrary) { Join-Path $capDir "no-dlc-library" } else { "" }
    $control = Invoke-Leg "control" $absent $absentLibrary 0 0 0
    $controlPass = $control.SongList -and -not $control.DlcSong -and
                   ($control.AggregateItems -le 0) -and -not $control.Fatal
}

$summary = [pscustomobject]@{
    ExpectedSongs = $expected; GameRoot = $GameRoot; DlcRoot = $DlcRoot; DlcLibrary = $DlcLibrary
    Dlc = $dlcPass; Control = $controlPass; SkipControl = [bool]$SkipControl
    DlcLeg = $dlc; ControlLeg = $control
}

Write-Host "`n=== DLC acceptance ==="
[pscustomobject]@{ DlcLeg = $dlcPass; ControlLeg = $controlPass; Expected = ($expected -join ", ") } |
    Format-List | Out-String | Write-Host
foreach ($leg in @($dlc, $control)) { if ($leg -and $leg.Notes) { Write-Host ("{0}: {1}" -f $leg.Leg, $leg.Notes) } }
$summary | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $capDir "summary.json")

$ok = $dlcPass -and $controlPass
Write-Host ("dlc: {0}" -f $(if ($ok) { "PASS" } else { "FAIL" }))
if (-not $ok) { exit 1 }
exit 0
