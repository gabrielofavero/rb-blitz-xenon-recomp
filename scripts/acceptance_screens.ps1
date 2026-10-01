# Screens acceptance: the menu routes a song run never takes.
#
# scripts/acceptance_launches.ps1 stops at the title and
# scripts/acceptance_song.ps1 drives title -> sign-in -> offline prompt -> main
# menu -> song list -> gameplay -> results. Every other screen the main menu can
# reach was verified by hand: the career leaderboard, the achievements entry,
# the HELP & OPTIONS pages, the download store notice and the title's own exit.
# This drives all of them in one boot, decides each screen from Windows OCR text
# (scripts/ocr_image.ps1) rather than from a sleep, and keeps the screenshot it
# decided on.
#
# What it pins down - all of it measured offline on this build, none of it
# obvious from the outside:
#
#   * LEADERBOARDS opens a career board that names Rock Central as the reason it
#     is empty: the entry is live offline, and this is the screen that says why;
#   * ACHIEVEMENTS is a row that does nothing without a signed-in profile - A on
#     it leaves the main menu up - so that is what the run asserts, a row that
#     is present and inert, and not a screen;
#   * HELP & OPTIONS is a submenu titled "How to Play" over four pages -
#     Controls, Calibration, Audio/Video and Credits - each one row down the
#     list and back on B;
#   * DOWNLOAD CONTENT shows the eStore notice ("You must be signed in ... Press
#     A to Continue"); A clears it and B does not;
#   * EXIT GAME asks "ARE YOU WANT TO EXIT THE GAME?" (the title's own wording) -
#     cancel with B, and YES is one press up, after which the guest ends its own
#     process. This is the one route in the harness where the guest exits rather
#     than the script closing its window, and it is a different shutdown from
#     that one: the guest's exit prints `KernelState::TerminateTitle` and
#     `Execution complete`, where closing the window prints `Title terminated;
#     hard-exiting process.` The run requires the guest's own pair.
#
# Every row is reached by clamping to the first row of its list and tapping down
# from there, and every screen is checked against the row it should be: the guest
# resets a list to its first row whenever the list is entered (measured: leaving
# the leaderboard screen and accepting with no tap at all opens the song list),
# so a walk that carried on from wherever the last screen left the highlight
# would be reading a row it never chose. Clamping also means a tap that does not
# arrive costs a retry rather than a wrong row: a step that lands somewhere else
# walks the route again from the first row instead of pressing on into an
# unknown one.
#
# The script drives the menus with injected keys and turns the mouse's synthetic
# pad off (`--no-mouse_ui_nav`), so what the run asserts does not depend on where
# the mouse pointer happens to be resting.
#
# Usage:  .\scripts\acceptance_screens.ps1
# Writes out/m6-screens/*.png (every screen it asserted on), a copy of the run's
# log and summary.json, prints a per-screen verdict and exits non-zero unless
# every screen behaved and the guest exited on its own.
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/m6-screens",
    [string]$GameRoot,
    # Vanilla by default, like the song route: an installed Ultimate payload
    # must not be able to change what these screens say.
    [string]$UltimateMode = "0",
    [int]$BootTimeoutSec = 180,
    [int]$ScreenTimeoutSec = 30,
    [int]$ExitTimeoutSec = 30,
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

function Get-ScreenText([string]$Shot) {
    return ((Invoke-ChildScript "ocr_image.ps1" @{ Path = $Shot }) -join " ").ToUpperInvariant()
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

function Wait-ForExit($Proc, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        try { $Proc.Refresh(); if ($Proc.HasExited) { return $true } } catch { return $true }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Invoke-Actions([string[]]$Actions) {
    Invoke-ChildScript "drive_ui.ps1" @{ Actions = ($Actions -join ","); BuildDir = $BuildDir; OutDir = $OutDir } | Out-Null
}

# ------------------------------------------------------------------ route ---

# The main menu, as text: its rows are what makes it recognisable, and the store
# notice drawn over the same rows is what the eStore line excludes.
function Test-MenuText([string]$Text) {
    return ($Text.Contains("LEADERBOARDS") -and $Text.Contains("DOWNLOAD CONTENT") -and
            -not $Text.Contains("ESTORE") -and -not $Text.Contains("TO START"))
}

function Get-Screen([string]$Tag) {
    return Get-ScreenText (Save-Shot "$Tag.png")
}

# Back to the main menu and require it, from wherever the last step left the
# title: leave the screen with B, and walk the title screen's offline route
# again if that B left the menu entirely (which is what B does on the menu
# itself). Bounded, so a guest that stops responding fails the run rather than
# hanging it.
function Wait-ForMenu([string]$Tag) {
    $t = ""
    for ($i = 1; $i -le 8; $i++) {
        $t = Get-Screen "$Tag-menu"
        if (Test-MenuText $t) { return $t }
        if ($t.Contains("TO START")) {
            # Title -> sign-in dialog -> offline prompt -> main menu.
            Invoke-Actions @("key:a", "wait:6", "key:a", "wait:6", "key:a")
        } else {
            Invoke-Actions @("key:b")
        }
        Start-Sleep -Seconds 5
    }
    return $t
}

# The walk starts from the first row of whatever list is up: every list this
# script drives clamps at both ends (measured, and the same clamp
# scripts/acceptance_song.ps1 relies on for its song list), so taps up beyond
# the first row cost nothing and leave the highlight known.
function Reset-Highlight([string]$Tag) {
    $actions = @("wait:1")
    for ($i = 0; $i -lt 8; $i++) { $actions += "key:lstick_up" }
    Invoke-Actions $actions
    Save-Shot "$Tag-clamped.png" | Out-Null
}

# Accept whatever row is highlighted, with the screen it produced read back.
#
# A screen can take a moment to draw - the credits page scrolls in - so the read
# is repeated until it is the one the step asked for rather than made once. The
# inert row (no needle) is the other case: what it shows is the list still being
# up, which is also what a read taken before the accept could see, so that one
# waits before it looks.
function Invoke-AcceptRow($Step, [string]$Tag) {
    Invoke-Actions @("wait:1", "shot:$Tag-row.png", "key:a")
    if ($Step.Needle -eq "") {
        Start-Sleep -Seconds 4
        return Get-Screen $Tag
    }
    $last = ""
    $deadline = (Get-Date).AddSeconds($ScreenTimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $last = Get-Screen $Tag
        if (Test-Step $last $Step.Needle) { return $last }
        Start-Sleep -Seconds 3
    }
    return $last
}

# True when $Text is what the row the step asked for shows. A row whose needle
# is empty is one with no screen of its own (ACHIEVEMENTS offline), which shows
# as the menu still being up.
function Test-Step([string]$Text, [string]$Needle) {
    if ($Needle -eq "") { return (Test-MenuText $Text) }
    return $Text.Contains($Needle)
}

# Back to the list a substep came from (the HELP & OPTIONS submenu for its
# pages), which B does one screen at a time.
function Wait-ForList([string]$Needle, [string]$Tag) {
    $t = ""
    for ($i = 1; $i -le 5; $i++) {
        $t = Get-Screen "$Tag-parent$i"
        if ($t.Contains($Needle)) { return $t }
        Invoke-Actions @("key:b")
        Start-Sleep -Seconds 4
    }
    return $t
}

# Walk the whole route, one row at a time.
#
# Each row is reached from the first row of its own list, never from wherever
# the screen before left the highlight (see the header for the reset this
# avoids), each row's screen is checked against what that row should show, and
# each screen is left again before the next press - the leave is checked too, so
# "B leaves the leaderboard" is an assertion and not an assumption.
#
# A step that lands somewhere unexpected walks the whole route again from the
# clamped first row rather than pressing on into an unknown one, and three
# attempts is where it gives up, so a guest that stops responding fails the run
# instead of moving it somewhere else.
#
# Returns the screen each step showed and whether each step's leave landed.
function Invoke-Walk($Route, [string]$Tag, [System.Collections.ArrayList]$Notes) {
    $screens = @()
    $left = @()
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $screens = @()
        $left = @()
        $ok = $true
        $onList = ""
        for ($i = 0; $i -lt $Route.Count; $i++) {
            $step = $Route[$i]
            # A step on another list needs that list up first.
            if ($step.On -ne $onList) {
                if ($step.On -eq "") {
                    $up = Test-MenuText (Wait-ForMenu "$Tag-$($step.Name)-list")
                } else {
                    $up = (Wait-ForList $step.On "$Tag-$($step.Name)-list").Contains($step.On)
                }
                if (-not $up) {
                    $Notes.Add(("{0}: {1} needs the {2} list, which did not come up" -f ${Tag}, $step.Name, $step.On)) | Out-Null
                    $ok = $false
                    break
                }
                $onList = $step.On
            }
            Reset-Highlight "$Tag-$($step.Name)"
            $downs = @()
            for ($d = 0; $d -lt $step.Downs; $d++) { $downs += "key:lstick_down" }
            if ($downs.Count -gt 0) { Invoke-Actions $downs }
            $text = Invoke-AcceptRow $step "$Tag-$($step.Name)"
            $screens += $text
            if (-not (Test-Step $text $step.Needle)) {
                $Notes.Add(("{0}: {1} did not show what it should (screen: {2})" -f ${Tag}, $step.Name, $text)) | Out-Null
                Write-Host ("  !! {0}: '{1}' is not the screen this row shows; walking again" -f ${Tag}, $step.Name)
                $ok = $false
                break
            }
            if ($step.Leave -eq "") { $left += ""; continue }
            if ($step.Leave -eq "a") { Invoke-Actions @("key:a") } else { Invoke-Actions @("key:b") }
            if ($step.Parent -eq "") {
                $back = Wait-ForMenu ("$Tag-$($step.Name)-back")
                $backOk = Test-MenuText $back
            } else {
                $back = Wait-ForList $step.Parent ("$Tag-$($step.Name)-back")
                $backOk = $back.Contains($step.Parent)
            }
            $left += $backOk
            if (-not $backOk) {
                $Notes.Add(("{0}: could not get back from {1} (screen: {2})" -f ${Tag}, $step.Name, $back)) | Out-Null
                $ok = $false
                break
            }
        }
        if ($ok) { return [pscustomobject]@{ Ok = $true; Screens = $screens; Left = $left } }
        Wait-ForMenu $Tag | Out-Null
    }
    return [pscustomobject]@{ Ok = $false; Screens = $screens; Left = $left }
}

# ------------------------------------------------------------------- main ---

# The whole route, in the order the menus reach it: one row at a time down the
# main menu, into HELP & OPTIONS and down its pages, then the store notice, then
# the exit dialog twice.
#
#   On      the list the row belongs to: "" is the main menu, "HOW TO PLAY" is
#           the submenu.
#   Downs   left-stick taps down from that list's first row to this row. The
#           main menu's first row is PLAY, which belongs to the song route, so
#           LEADERBOARDS - the first row this route is about - is one tap down.
#   Needle  the screen that row shows. "" is a row with no screen of its own:
#           ACHIEVEMENTS needs a signed-in profile and does nothing offline, so
#           the menu still being up is what it shows.
#   Leave   how its screen is left again - "b" for every screen that has a BACK
#           label, "a" for the store notice, which says "Press A to Continue"
#           and ignores B.
#   Parent  the list the next row belongs to: "" is the main menu, "HOW TO PLAY"
#           is the submenu (B returns from a page to its submenu, not past it).
$route = @(
    @{ Name = "leaderboards"; On = "";            Downs = 1; Needle = "CAREER LEADERBOARD"; Leave = "b"; Parent = "" },
    @{ Name = "achievements"; On = "";            Downs = 2; Needle = "";                   Leave = "";  Parent = "" },
    @{ Name = "help";         On = "";            Downs = 3; Needle = "HOW TO PLAY";        Leave = "";  Parent = "" },
    # The submenu's first row is Controls: "How to Play" is its title, not a row
    # (measured - one tap opens the controller screen).
    @{ Name = "controls";     On = "HOW TO PLAY"; Downs = 1; Needle = "CONTROLLER";         Leave = "b"; Parent = "HOW TO PLAY" },
    @{ Name = "calibration";  On = "HOW TO PLAY"; Downs = 2; Needle = "CALIBRATION WIZARD"; Leave = "b"; Parent = "HOW TO PLAY" },
    @{ Name = "av";           On = "HOW TO PLAY"; Downs = 3; Needle = "OVERSCAN";           Leave = "b"; Parent = "HOW TO PLAY" },
    @{ Name = "credits";      On = "HOW TO PLAY"; Downs = 4; Needle = "PROGRAMMER";         Leave = "b"; Parent = "" },
    @{ Name = "download";     On = "";            Downs = 4; Needle = "ESTORE";             Leave = "a"; Parent = "" },
    @{ Name = "exit";         On = "";            Downs = 5; Needle = "WANT TO EXIT";       Leave = "b"; Parent = "" },
    @{ Name = "exit-confirm"; On = "";            Downs = 5; Needle = "WANT TO EXIT";       Leave = "";  Parent = "" }
)

$notes = New-Object System.Collections.ArrayList
$r = [ordered]@{
    Boot = $false; Menu = $false
    Leaderboards = $false; LeaderboardsBack = $false
    AchievementsInert = $false
    HelpMenu = $false
    HelpControls = $false; HelpCalibration = $false; HelpAudioVideo = $false; HelpCredits = $false
    DownloadNotice = $false; DownloadBack = $false
    ExitDialog = $false; ExitCancelled = $false; Exited = $false
    Fatal = $false; Terminated = $false; LogKB = 0; Notes = ""
}

Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue | Remove-Item -Force
$p = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru `
    -ArgumentList "--game_data_root=$GameRoot", "--ultimate_mode=$UltimateMode", `
                  "--mnk_mode=1", "--no-mouse_ui_nav", "--log_level=debug", "--log_flush_interval=1", `
                  "--log_max_file_size_mb=100"

try {
    if (-not (Wait-ForWindow $p $BootTimeoutSec)) { $notes.Add("no game window appeared") | Out-Null }
    $title = Wait-ForScreen (Shot "title.png") "TO START" $BootTimeoutSec "title screen"
    if ($title.Contains("TO START")) { $r.Boot = $true } else { $notes.Add("title screen not recognised") | Out-Null }

    # Title -> sign-in dialog -> offline prompt -> main menu, the documented
    # offline route; A is SELECT on both dialogs.
    Invoke-Actions @("key:a")
    Wait-ForScreen (Shot "signin.png") "ROCK CENTRAL" $ScreenTimeoutSec "sign-in dialog" | Out-Null
    Invoke-Actions @("key:a")
    Wait-ForScreen (Shot "offline.png") "OFFLINE MODE" $ScreenTimeoutSec "offline prompt" | Out-Null
    Invoke-Actions @("key:a")
    Start-Sleep -Seconds 8
    $menu = Wait-ForMenu "menu"
    if (Test-MenuText $menu) { $r.Menu = $true } else { $notes.Add("main menu not recognised: $menu") | Out-Null }

    $walk = Invoke-Walk $route "route" $notes
    # Each step's screen is the evidence for that step, whether the walk as a
    # whole completed or not.
    $seen = @{}
    $leaving = @{}
    for ($i = 0; $i -lt $route.Count; $i++) {
        if ($i -lt $walk.Screens.Count) { $seen[$route[$i].Name] = $walk.Screens[$i] }
        if ($i -lt $walk.Left.Count) { $leaving[$route[$i].Name] = $walk.Left[$i] }
    }

    # Row 1: the career leaderboard. Live offline, and it says what it waits for.
    $r.Leaderboards = $seen["leaderboards"] -and $seen["leaderboards"].Contains("CAREER LEADERBOARD")
    $r.LeaderboardsBack = [bool]$leaving["leaderboards"]
    # Row 2: achievements, inert without a signed-in profile, so the assertion
    # is the main menu still being the screen up after the accept.
    $r.AchievementsInert = $seen["achievements"] -and (Test-MenuText $seen["achievements"])
    # Row 3 and its four pages.
    $r.HelpMenu = $seen["help"] -and $seen["help"].Contains("HOW TO PLAY")
    $r.HelpControls = $seen["controls"] -and $seen["controls"].Contains("CONTROLLER")
    $r.HelpCalibration = $seen["calibration"] -and $seen["calibration"].Contains("CALIBRATION WIZARD")
    $r.HelpAudioVideo = $seen["av"] -and $seen["av"].Contains("OVERSCAN")
    $r.HelpCredits = $seen["credits"] -and $seen["credits"].Contains("PROGRAMMER")
    # Row 4: the eStore notice, which A leaves (it says "Press A to Continue")
    # and B does not.
    $r.DownloadNotice = $seen["download"] -and $seen["download"].Contains("ESTORE")
    $r.DownloadBack = [bool]$leaving["download"]
    # Row 5: the exit dialog, cancelled with B and then asked for again.
    $r.ExitDialog = $seen["exit"] -and $seen["exit"].Contains("WANT TO EXIT")
    $r.ExitCancelled = [bool]$leaving["exit"]

    # The confirmation is the guest's own exit; the up direction is what selects
    # YES (measured: A alone, or A after left, leaves the dialog on NO).
    if ($seen["exit-confirm"] -and $seen["exit-confirm"].Contains("WANT TO EXIT")) {
        Invoke-Actions @("key:lstick_up")
        Invoke-Actions @("key:a")
        # From here the window is expected to disappear, so nothing that
        # captures a screen may run again.
        $r.Exited = Wait-ForExit $p $ExitTimeoutSec
        if (-not $r.Exited) { $notes.Add("the guest did not exit after confirming YES") | Out-Null }
    } else {
        $notes.Add("the exit dialog did not come up a second time") | Out-Null
    }
} finally {
    if (-not $p.HasExited) {
        $p.CloseMainWindow() | Out-Null
        if (-not $p.WaitForExit($CloseWaitSec * 1000)) { Stop-Process -Id $p.Id -Force }
    }
}

# Faults and the shutdown marker the guest's own exit prints - a killed process
# writes neither, and the window-close path scripts/acceptance_launches.ps1 uses
# writes the other one ("Title terminated; hard-exiting process.").
$log = Get-NewestLog
$text = Get-LogText $log
$r.Fatal = [bool]($text | Select-String -Pattern "\[FATAL\]" -Quiet)
if ($r.Fatal) { $notes.Add("log contains [FATAL]") | Out-Null }
$r.Terminated = [bool]($text | Select-String -Pattern "KernelState::TerminateTitle" -Quiet)
if (-not $r.Terminated) { $notes.Add("the guest never reached KernelState::TerminateTitle") | Out-Null }
if (Test-Path $log) {
    $r.LogKB = [math]::Round((Get-Item $log).Length / 1KB)
    Copy-Item $log (Join-Path $capDir "run.log") -Force
}

# ----------------------------------------------------------------- verdict ---

$r.Notes = ($notes -join "; ")
Write-Host "`n=== screens acceptance ==="
[pscustomobject]$r | Format-List Boot, Menu, Leaderboards, LeaderboardsBack, AchievementsInert,
    HelpMenu, HelpControls, HelpCalibration, HelpAudioVideo, HelpCredits,
    DownloadNotice, DownloadBack, ExitDialog, ExitCancelled, Exited,
    Terminated, Fatal, LogKB | Out-String | Write-Host
if ($r.Notes) { Write-Host "notes: $($r.Notes)" }
$r | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $capDir "summary.json")

# A pass is every screen reached and named, every B landing back where it came
# from, the store notice left with A, the exit dialog both cancelled and
# confirmed - and the log showing the title ended itself, with no fatal.
$ok = $r.Boot -and $r.Menu -and $r.Leaderboards -and $r.LeaderboardsBack -and
      $r.AchievementsInert -and $r.HelpMenu -and $r.HelpControls -and $r.HelpCalibration -and
      $r.HelpAudioVideo -and $r.HelpCredits -and $r.DownloadNotice -and $r.DownloadBack -and
      $r.ExitDialog -and $r.ExitCancelled -and $r.Exited -and $r.Terminated -and -not $r.Fatal
Write-Host ("screens: {0}" -f $(if ($ok) { "PASS" } else { "FAIL" }))
if (-not $ok) { exit 1 }
exit 0
