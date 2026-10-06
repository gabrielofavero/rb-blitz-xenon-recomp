# Capture the launcher's own UI and assert what a picture can prove about it.
#
# E2 of docs/plans/launcher-plan.md. The game's harness sends keys to rb_blitz and reads
# the guest's screens (scripts/drive_ui.ps1, scripts/observe_ui.ps1); this one sends the
# same kind of keys to rb_blitz_launcher and reads *its* pixels, because the launcher is
# an ordinary Windows window with an ordinary focus model and the claims worth proving
# are its own: a tab switch changes the body, a press that leaves a row rewrites the bar's
# help line, and one press moves the ring one focus entry.
#
# It joins the scripts that already exist rather than re-implementing either:
#
#   scripts/capture_window.ps1 -ClientArea   saves the pixels the launcher drew
#   scripts/frame_diff.ps1                   measures two crops against each other
#
# and it prefers the launcher's own headless output wherever text can carry the claim
# instead of a percentage. All of --dump-layout, --dump-display, --dump-profile,
# --dump-general and --print-command run without a window (launcher/main.cpp), and the
# focused row is written to --focus-log as it changes (A3). The pixel checks are the
# corroboration for the two claims text cannot make: that the *screen* changed, in the
# region the claim is about, and that nothing else moved.
#
# The regions, in the launcher's own units. The launcher opens maximized (D18), so the
# window is the work area the machine provides and there is no size to assume: the crops
# are taken from the capture's real dimensions and from two heights the shell computes -
# BottomBarHeight() in launcher/src/shell.cpp (the line of controls, two help lines, the
# window's bottom padding) and the tab strip above the body - scaled by the display's
# content scale, which --dump-display reports. The help crop is the bar's *help lines*
# only, not the whole bar: the ring's own highlight moves with the focus too, and a crop
# that contained both would pass whether or not the help line changed.
#
# Keys are sent with keybd_event (Win32), the way scripts/drive_ui.ps1 sends them to the
# game, with the scan code MapVirtualKey derives rather than a table of our own. Two
# wrinkles are worth stating because a silently-ignored key is the failure mode here:
#
#   * the arrow and navigation keys are *extended* keys, and the injected event has to
#     say so (KEYEVENTF_EXTENDEDKEY); measured on this machine, Down without it moved
#     nothing while Right with it changed the tab;
#   * the window has to own the foreground *and* the keyboard focus before a key is
#     sent - Windows withholds both from a background process, and capture_window.ps1
#     taps Alt for the same reason - so every send re-asserts it and throws if it will
#     not converge.
#
# Usage:
#   .\scripts\capture_launcher.ps1                    # headless leg + keyboard leg + pad leg
#   .\scripts\capture_launcher.ps1 -SkipWindow        # the headless assertions alone
#   .\scripts\capture_launcher.ps1 -DownPresses 4     # a shorter walk of the ring
#
# Evidence lands in out/launcher-capture/: the frames, the crops each percentage came
# from, both --focus-logs, the headless reports, and summary.json. Exits 0 when every
# check held and non-zero otherwise, like the acceptance scripts.
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/launcher-capture",
    [string]$GameRoot,
    # The profile the run is pinned to. The script *writes* it (a fixture with a known
    # target and a known window size) every run, which is what makes two runs comparable,
    # and then asserts the run did not change it: only Save writes the profile (B4), and
    # a harness that pressed nothing has nothing to save.
    [string]$ProfilePath,
    # The fixture's launch target. The retail game is the default because it needs
    # nothing but a dump; -Target ultimate also requires the payload under the game root.
    [string]$Target = "common",
    # How far down the ring the harness walks. Six leaves the launch target's three choices
    # and puts the ring on three more rows, which is what gives the walk a row boundary to
    # cross and the help line something to say.
    [int]$DownPresses = 6,
    # The scripted pad the pad leg attaches (launcher/src/virtual_pad.h). Empty skips the
    # leg; -SkipPadLeg is the same thing spelled out loud.
    [string]$PadScript = "family=xbox;down:200;down:200",
    [switch]$SkipWindow,
    [switch]$SkipPadLeg,
    # A5's three legs, each a switch so a session can run the one it is working on:
    #   -SkipKeys         the rebinding leg (bind J, save, put it back)
    #   -SkipWalkthrough  the lap of every tab's ring, which is the "never traps focus" check
    #   -SkipScale        the four-scale leg, which is the focus-ring-at-every-DPI-step check
    #   -SkipSafeMode     the locked-out-profile leg
    [switch]$SkipKeys,
    [switch]$SkipWalkthrough,
    [switch]$SkipScale,
    [switch]$SkipSafeMode,
    # A few pixels of jitter are not a moved UI: the control pair is the *same* state
    # captured twice, and what it is allowed to differ by. Measured on this machine, it
    # differs by none at all.
    [double]$NoisePercent = 0.02,
    # A focus move has to move at least this much of the help strip, and a tab switch at
    # least this much of the body. Both are floors well under what was measured, so a
    # real change cannot fall through them and a missing change cannot pass.
    [double]$HelpChangePercent = 0.5,
    [double]$BodyChangePercent = 2.0,
    # ...and this much of the body for a single press, which moves the ring's highlight
    # rather than replacing the tab's content: measured on this machine a press inside the
    # launch target's three choices moves 0.1-0.2% of the body crop, and leaving the row
    # moves more.
    [double]$PressBodyPercent = 0.05,
    # Both crops are downscaled by this before frame_diff walks them: the comparison costs
    # the crop's area in PowerShell, and the claims here ("the body changed") are about
    # whole rows of text rather than single pixels.
    [int]$CropScale = 4,
    # A5's scale leg: the UI scales the harness re-measures the two crops at, with
    # --ui-scale. The machine's own scale is not in the list because the keyboard leg above
    # already runs at it (measured: 3.0 here), which is how the leg has four steps on one
    # machine rather than one.
    [double[]]$UiScales = @(1.0, 1.5, 2.0),
    # The walkthrough leg's budget: Home plus this many Downs per tab. More than any ring in the
    # launcher has (the largest is the Controller tab's 37 entries), and deliberately blind -
    # the leg presses first and reads the trace once at the end, because a harness that reads the
    # log between presses is measuring its own timing.
    [int]$LapPresses = 45,
    [int]$SettleSeconds = 2,
    [int]$WindowTimeoutSec = 40
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work "rb_blitz_launcher.exe"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
$capDir = Join-Path $root $OutDir
$frameDir = Join-Path $capDir "frames"
$cropDir = Join-Path $capDir "crops"
if (-not $ProfilePath) { $ProfilePath = Join-Path $capDir "profile\launcher.toml" }
New-Item -ItemType Directory -Force -Path $capDir, $frameDir, $cropDir | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ProfilePath) | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) {
    throw "no default.xex under ${GameRoot}: the launcher needs a game root to describe"
}

# ------------------------------------------------------------------ the checks ---

# Every check the run makes, in the order it made it: one object per check, with what it
# looked at. The summary file is these rows, so a failure is answerable without the
# console the script did not run in.
$checks = New-Object System.Collections.ArrayList
function Add-Check([string]$Leg, [string]$Check, [bool]$Ok, [string]$Detail = "") {
    $checks.Add([pscustomobject]@{ Leg = $Leg; Check = $Check; Ok = $Ok; Detail = $Detail }) | Out-Null
}

function Invoke-Child([string]$Script, [hashtable]$Named) {
    # This machine's execution policy is Restricted (docs/build-and-run.md section 0), so
    # every sibling script goes through a child PowerShell - the same shape
    # scripts/observe_ui.ps1 uses. A boolean value names a switch, which is passed as the
    # bare flag: "-ClientArea true" would be read as a positional argument.
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) {
        if ($Named[$k] -is [bool]) {
            if ($Named[$k]) { $argv += "-$k" }
        } else {
            $argv += @("-$k", [string]$Named[$k])
        }
    }
    $out = & powershell @argv 2>&1
    return [pscustomobject]@{ Exit = $LASTEXITCODE; Text = (($out | Out-String).Trim()) }
}

# The launcher is a WIN32-subsystem executable: it has no console, so every report it
# makes is a file. Each of the dump modes takes the same two path flags the windowed run
# will get, so what it reports is what the window would show.
function Invoke-Dump([string]$Mode) {
    $file = Join-Path $capDir "$Mode.txt"
    if (Test-Path $file) { Remove-Item $file -Force }
    $arguments = @("--$Mode=$file", "--launcher_profile=$ProfilePath", "--game_data_root=$GameRoot")
    $p = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru
    $text = if (Test-Path $file) { (Get-Content -LiteralPath $file -Raw) } else { "" }
    return [pscustomobject]@{ Exit = $p.ExitCode; Text = $text }
}

# --------------------------------------------------------------- the picture ---

Add-Type -AssemblyName System.Drawing

# The height of the pieces the shell reserves, in the points launcher/main.cpp gives
# ImGui (style.WindowPadding 18x16, style.FramePadding 12x7, style.ItemSpacing 12x12,
# kUiFontSize 16, kHelpLines 2 in launcher/src/shell.cpp) - and then multiplied by the
# display's content scale, which is what the shell's own arithmetic does (ScaleAllSizes).
function Get-Region([int]$Scale, [int]$Width, [int]$Height) {
    $font = 16
    $frameHeight = $font + 2 * 7
    # BottomBarHeight(): the controls line, the help lines, the window's bottom padding,
    # with the style's item spacing between the two lines.
    $barPoints = $frameHeight + 12 + 2 * $font + 16
    # The help lines alone: what the row's tooltip is drawn in, below the controls line.
    $helpPoints = 2 * $font + 16
    # Above the body: the window's top padding, the tab strip's buttons, the separator
    # under them and the spacing around it. Taken a little generously - a crop that starts
    # a few pixels into the body is still the body, and a crop that started in the strip
    # would be a crop of the tab buttons.
    $stripPoints = 16 + $frameHeight + 2 * 12 + 4
    $barPixels = [int][Math]::Round($barPoints * $Scale)
    $helpPixels = [int][Math]::Round($helpPoints * $Scale)
    $stripPixels = [int][Math]::Round($stripPoints * $Scale)
    $body = [pscustomobject]@{
        X = 0; Y = $stripPixels; W = $Width; H = [Math]::Max(1, $Height - $barPixels - $stripPixels)
    }
    $help = [pscustomobject]@{
        X = 0; Y = [Math]::Max(0, $Height - $helpPixels); W = $Width; H = [Math]::Min($helpPixels, $Height)
    }
    return [pscustomobject]@{ Body = $body; Help = $help; BarPixels = $barPixels; Scale = $Scale }
}

# One region of one frame, written as its own PNG and downscaled: frame_diff reads files.
function Save-Crop([string]$Frame, [string]$Region, [pscustomobject]$Box, [int]$Index) {
    $source = [System.Drawing.Image]::FromFile($Frame)
    try {
        $w = [int][Math]::Max(1, $Box.W / $CropScale)
        $h = [int][Math]::Max(1, $Box.H / $CropScale)
        $bitmap = New-Object System.Drawing.Bitmap $w, $h
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        # Nearest-neighbour, not interpolation: a diff is meant to be a diff, and a
        # smoothed-downscale would spread a change over its neighbours and blur small ones
        # into the noise floor.
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $destination = New-Object System.Drawing.Rectangle 0, 0, $w, $h
        $origin = New-Object System.Drawing.Rectangle $Box.X, $Box.Y, $Box.W, $Box.H
        $graphics.DrawImage($source, $destination, $origin, [System.Drawing.GraphicsUnit]::Pixel)
        $graphics.Dispose()
        $path = Join-Path $cropDir ("{0}-{1:D2}.png" -f $Region, $Index)
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $bitmap.Dispose()
        return $path
    } finally {
        $source.Dispose()
    }
}

function Get-FrameSize([string]$Frame) {
    $image = [System.Drawing.Image]::FromFile($Frame)
    try { return [pscustomobject]@{ W = $image.Width; H = $image.Height } } finally { $image.Dispose() }
}

# The percentage of pixels scripts/frame_diff.ps1 sees move between two crops. No change
# is 0, a size mismatch is -1 (it cannot be a percentage), and a failure to run at all is
# -2, so a broken tool is never mistaken for a clean pair.
function Get-DiffPercent([string]$A, [string]$B) {
    $result = Invoke-Child "frame_diff.ps1" @{ Files = "$A,$B"; Tolerance = 8 }
    if ($result.Text -match "SIZE MISMATCH") { return -1.0 }
    if ($result.Text -match "([0-9.]+)% pixels differ") { return [double]$Matches[1] }
    return -2.0
}

# ------------------------------------------------------------------ the input ---

if (-not ("RbLauncherWin32" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class RbLauncherWin32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
}
"@
}

# The keys this harness sends, and whether the injected event has to say it is extended.
# The navigation cluster is extended on a real keyboard; the letters, Tab, Enter, Space
# and Escape are not. `Modifier` is the virtual key held down around the press, which is
# how a chord is sent: A5 binds Ctrl+S to Save and Ctrl+C to the command line, so the
# harness has to be able to press one.
$keys = @{
    "down"      = @{ Vk = 0x28; Extended = $true }
    "up"        = @{ Vk = 0x26; Extended = $true }
    "right"     = @{ Vk = 0x27; Extended = $true }
    "left"      = @{ Vk = 0x25; Extended = $true }
    "home"      = @{ Vk = 0x24; Extended = $true }
    "end"       = @{ Vk = 0x23; Extended = $true }
    "tab"       = @{ Vk = 0x09; Extended = $false }
    "enter"     = @{ Vk = 0x0D; Extended = $false }
    "space"     = @{ Vk = 0x20; Extended = $false }
    "escape"    = @{ Vk = 0x1B; Extended = $false }
    # Letters, for the rebinding leg: J is a key no default binding names.
    "j"         = @{ Vk = 0x4A; Extended = $false }
    "s"         = @{ Vk = 0x53; Extended = $false }
    "c"         = @{ Vk = 0x43; Extended = $false }
    "ctrl+s"    = @{ Vk = 0x53; Extended = $false; Modifier = 0x11 }
    "ctrl+c"    = @{ Vk = 0x43; Extended = $false; Modifier = 0x11 }
}

function Get-LauncherWindow([int]$ProcessId, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
        if ($null -eq $p -or $p.HasExited) { return [IntPtr]::Zero }
        if ($p.MainWindowHandle -ne [IntPtr]::Zero -and $p.MainWindowTitle) { return $p.MainWindowHandle }
        Start-Sleep -Milliseconds 250
    }
    return [IntPtr]::Zero
}

function Get-WindowTitle([int]$ProcessId) {
    $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $p) { return "" }
    return $p.MainWindowTitle
}

function Focus-LauncherWindow([int]$ProcessId) {
    $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $p) { throw "the launcher exited before a key could be sent" }
    $h = $p.MainWindowHandle
    if ($h -eq [IntPtr]::Zero) { throw "the launcher has no main window" }
    if ([RbLauncherWin32]::IsIconic($h)) { [RbLauncherWin32]::ShowWindow($h, 9) | Out-Null }
    for ($i = 0; $i -lt 25; $i++) {
        if ([RbLauncherWin32]::GetForegroundWindow() -eq $h) {
            Start-Sleep -Milliseconds 200
            return $h
        }
        # Tapping Alt is the documented way to hand this process the foreground rights
        # Windows withholds from a background one - the same trick capture_window.ps1 and
        # drive_ui.ps1 use.
        [RbLauncherWin32]::keybd_event(0x12, 0x38, 0, [IntPtr]::Zero)
        [RbLauncherWin32]::keybd_event(0x12, 0x38, 2, [IntPtr]::Zero)
        [RbLauncherWin32]::BringWindowToTop($h) | Out-Null
        [RbLauncherWin32]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 150
    }
    throw "could not foreground the launcher window; a key sent now would reach another application"
}

function Send-KeyToLauncher([int]$ProcessId, [string]$Name) {
    if (-not $keys.ContainsKey($Name)) { throw "unknown key: $Name" }
    $key = $keys[$Name]
    $h = Focus-LauncherWindow $ProcessId
    $scan = [RbLauncherWin32]::MapVirtualKey([uint32]$key.Vk, 0)
    $flags = if ($key.Extended) { [uint32]1 } else { [uint32]0 }
    if ($key.Modifier) {
        # Held around the key, not sent as its own press: the launcher reads the modifiers
        # from ImGui's state, which is what makes "Ctrl+S" one binding and "S" another.
        $modifierScan = [RbLauncherWin32]::MapVirtualKey([uint32]$key.Modifier, 0)
        [RbLauncherWin32]::keybd_event([byte]$key.Modifier, [byte]$modifierScan, 0, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 60
    }
    [RbLauncherWin32]::keybd_event([byte]$key.Vk, [byte]$scan, $flags, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 120
    [RbLauncherWin32]::keybd_event([byte]$key.Vk, [byte]$scan, ($flags -bor 2), [IntPtr]::Zero)
    if ($key.Modifier) {
        Start-Sleep -Milliseconds 60
        [RbLauncherWin32]::keybd_event([byte]$key.Modifier, [byte]$modifierScan, 2, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 250
}

function Save-Frame([string]$Name) {
    $path = Join-Path $frameDir "$Name.png"
    if (Test-Path $path) { Remove-Item $path -Force }
    $result = Invoke-Child "capture_window.ps1" @{
        OutFile = $path; ProcessName = "rb_blitz_launcher"; ClientArea = $true
    }
    return [pscustomobject]@{ Path = $path; Ok = (Test-Path $path); Text = $result.Text }
}

# The focus trace's own lines: "<ms>ms focus tab=<tab> entry=<i>/<n> row=<key>". Only the
# focus lines, and only the fields the assertions read.
#
# The lines come back inside an object rather than as an array, and every caller reads
# `.Lines`. PowerShell unrolls an array that crosses a function boundary, so a one-line trace
# returned directly arrives as the *line*: `$trace.Count` would then be the line's own Count
# field (the ring's size, 34) instead of the number of lines, and `$trace[0]` would index into
# nothing. Neither the comma operator nor @() around the call is reliable across two nested
# functions, and a field is.
function Get-FocusTrace([string]$Path) {
    $rows = New-Object System.Collections.ArrayList
    if (Test-Path $Path) {
        foreach ($line in Get-Content -LiteralPath $Path) {
            if ($line -match "focus tab=(\S+) entry=(\d+)/(\d+)( row=(\S+))?") {
                $rows.Add([pscustomobject]@{
                    Tab = $Matches[1]; Entry = [int]$Matches[2]; Count = [int]$Matches[3]
                    Row = if ($Matches[5]) { $Matches[5] } else { "" }
                }) | Out-Null
            }
        }
    }
    return [pscustomobject]@{ Lines = $rows.ToArray() }
}

# The same lines, but only once the launcher has written its first one: a log that has been
# opened but not yet written is an empty answer, and an empty answer read as "the ring has
# nowhere to go" is the harness's mistake rather than the launcher's.
function Wait-FocusTrace([string]$Path, [int]$TimeoutSec = 15) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $lines = (Get-FocusTrace $Path).Lines
        if ($lines.Count -gt 0) { return [pscustomobject]@{ Lines = $lines } }
        Start-Sleep -Milliseconds 250
    }
    return [pscustomobject]@{ Lines = @() }
}

# The row of the launcher-keys block the ring is on, as --focus-log names it ("next:assign"),
# or empty when the ring is somewhere else.
function Get-FocusedKeyRow([string]$Path) {
    $lines = @((Get-FocusTrace $Path).Lines)
    if ($lines.Count -eq 0) { return "" }
    $row = $lines[$lines.Count - 1].Row
    if ($row -match "^launcher-key:(.+)$") { return $Matches[1] }
    return ""
}

# Down presses until the ring is on the row asked for, or the budget runs out. The block is the
# General tab's second, and this is how a leg gets to one of its twenty-five buttons without a
# second copy of the ring's arithmetic in the harness. The trace is re-read once more at the end,
# because the last press's line is the answer to the question this function is asked.
function Step-ToKeyRow([int]$ProcessId, [string]$Path, [string]$Wanted, [int]$Budget = 90) {
    for ($press = 0; $press -lt $Budget; $press++) {
        if ((Get-FocusedKeyRow $Path) -eq $Wanted) { return $true }
        Send-KeyToLauncher $ProcessId "down"
    }
    return ((Get-FocusedKeyRow $Path) -eq $Wanted)
}

# The profile with its two window numbers masked, which is what two profiles have to look
# like to be "the same file apart from the geometry A1 keeps by itself", and those two
# numbers read back for the report.
function ConvertTo-NormalizedProfile([string]$Text) {
    return ($Text -replace "(?m)^width = \d+", "width = ?" -replace "(?m)^height = \d+", "height = ?").Trim()
}

function Get-ProfileGeometry([string]$Text) {
    $w = if ($Text -match "(?m)^width = (\d+)") { $Matches[1] } else { "?" }
    $h = if ($Text -match "(?m)^height = (\d+)") { $Matches[1] } else { "?" }
    return "$($w)x$($h)"
}


# The fixture profile: the target the run is pinned to, a known window size, and nothing
# else - every other row is then the build's own default, which is what the body of a tab
# shows. Rendered, not copied from the machine's own profile: a harness that inherited the
# developer's settings would measure those instead.
function Write-Fixture([string]$Path, [string]$TargetName, [string]$Extra = "") {
    $text = @"
schema_version = 1

[launcher]
version = 1
portable = false

[window]
width = 1280
height = 840

[launch]
target = "$TargetName"
game_dir = ""
user_data_dir = ""
dlc_dir = ""
"@
    if ($Extra) { $text += "`n$Extra`n" }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    Set-Content -LiteralPath $Path -Value $text -NoNewline -Encoding ascii
}

# One windowed leg: start the launcher with these arguments, wait for its window, and hand
# back both so the caller can drive it. Every A5 leg does this and none of them needs a
# different kind of start, which is why it is one function rather than four copies.
function Start-Launcher([string[]]$Arguments) {
    $running = @(Get-Process rb_blitz_launcher -ErrorAction SilentlyContinue)
    if ($running.Count -ne 0) {
        throw "a launcher is already running (pid $($running[0].Id)); close it and run again"
    }
    $process = Start-Process -FilePath $exe -ArgumentList $Arguments -PassThru
    $hwnd = Get-LauncherWindow $process.Id $WindowTimeoutSec
    return [pscustomobject]@{ Process = $process; Window = $hwnd }
}

# --------------------------------------------------------------- headless leg ---

Write-Fixture $ProfilePath $Target
$fixtureRaw = Get-Content -LiteralPath $ProfilePath -Raw
$fixtureText = ConvertTo-NormalizedProfile $fixtureRaw

$layout = Invoke-Dump "dump-layout"
Add-Check "headless" "--dump-layout runs without a window" ($layout.Exit -eq 0 -and $layout.Text -ne "") "exit $($layout.Exit)"
foreach ($tab in @("general", "graphics", "controller")) {
    Add-Check "headless" "the '$tab' tab reaches the layout" ($layout.Text -match "tab ${tab}: \d+ rows") ""
}
Add-Check "headless" "every row carries a tooltip" ($layout.Text -match "every row carries a tooltip") ""
Add-Check "headless" "the launch target offers all three choices" `
    ($layout.Text -match "row\s+launch\.target.*choices: common, demo, ultimate") ""

$display = Invoke-Dump "dump-display"
$uiScale = 1.0
if ($display.Text -match "ui scale\s+: ([0-9.]+)") { $uiScale = [double]$Matches[1] }
Add-Check "headless" "--dump-display reports the UI scale" ($display.Exit -eq 0 -and $uiScale -gt 0) `
    "ui scale $uiScale"
Add-Check "headless" "--dump-display reports the face it loaded" ($display.Text -match "font face\s+: \S+") ""

$profile = Invoke-Dump "dump-profile"
Add-Check "headless" "the run is pinned to the fixture profile" `
    ($profile.Text -match [regex]::Escape("settings file  : $ProfilePath")) $ProfilePath
Add-Check "headless" "the fixture's folder is writable" ($profile.Text -match "writable\s+: yes") ""

$general = Invoke-Dump "dump-general"
Add-Check "headless" "the General tab finds the game root" `
    ($general.Text -match "game root\s+: .*\(found\)") ""
Add-Check "headless" "the stored target is the fixture's" `
    ($general.Text -match "target\s+: $Target \(stored $Target\)") $Target

$command = Invoke-Dump "print-command"
$expectedMode = if ($Target -eq "ultimate") { "1" } else { "0" }
Add-Check "headless" "the printed command line is the launch contract" `
    ($command.Text -match [regex]::Escape("--game_data_root=`"$GameRoot`"") -and
     $command.Text -match "--ultimate_mode=$expectedMode" -and
     $command.Text -match [regex]::Escape("--launcher_profile=`"$ProfilePath`"")) `
    (($command.Text -split "`n" | Where-Object { $_ -match "command\s+:" }) -join " ").Trim()
if ($Target -eq "demo") {
    Add-Check "headless" "the demo target passes the licence mask" ($command.Text -match "--license_mask=0") ""
}

if ($SkipWindow) {
    Write-Host "skipping the window legs (-SkipWindow)"
}

# --------------------------------------------------------- the windowed legs ---

$summary = $null
$keyboard = $null
$pad = $null
$keyBindings = $null
$walkthrough = $null
$scales = $null
$safeMode = $null
# The walk's rows are filled in by the keyboard leg and read by the report, so the list is made
# here: a run with -SkipWindow has no walk, and the summary has to be able to say so.
$walk = New-Object System.Collections.ArrayList
$diffs = New-Object System.Collections.ArrayList

function Stop-Launcher([System.Diagnostics.Process]$Process) {
    if ($null -eq $Process) { return $false }
    $p = Get-Process -Id $Process.Id -ErrorAction SilentlyContinue
    if ($null -eq $p) { return $true }
    # Escape leaves the shell (A1), and the launcher then exits on its own. A window that
    # has to be killed is a failure the caller reports rather than a harness detail.
    Escape-Launcher $Process.Id
    if ($Process.WaitForExit(15000)) { return $true }
    Stop-Process -Id $Process.Id -Force
    return $false
}

function Escape-Launcher([int]$ProcessId) {
    $p = Get-Process -Id $ProcessId -ErrorAction SilentlyContinue
    if ($null -eq $p -or $p.MainWindowHandle -eq [IntPtr]::Zero) { return }
    Send-KeyToLauncher $ProcessId "escape"
}

if (-not $SkipWindow) {
    $running = @(Get-Process rb_blitz_launcher -ErrorAction SilentlyContinue)
    if ($running.Count -ne 0) {
        # capture_window.ps1 picks the window by process name: with two launchers up the
        # picture could be either one, which is worse than not running.
        throw "a launcher is already running (pid $($running[0].Id)); close it and run again"
    }

    $focusLog = Join-Path $capDir "keyboard-focus.log"
    if (Test-Path $focusLog) { Remove-Item $focusLog -Force }
    $arguments = @(
        "--launcher_profile=$ProfilePath", "--game_data_root=$GameRoot",
        "--focus-log=$focusLog", "--no-gamepad"
    )
    # --no-gamepad: a pad plugged into the machine is not allowed to move the ring, or the
    # "one press, one row" count would be a count of the pad's presses too. The pad has its
    # own leg below.
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    $hwnd = Get-LauncherWindow $process.Id $WindowTimeoutSec
    Add-Check "window" "the launcher opens a window" ($hwnd -ne [IntPtr]::Zero) "pid $($process.Id)"
    if ($hwnd -eq [IntPtr]::Zero) {
        Stop-Launcher $process | Out-Null
        throw "the launcher never opened a window; nothing below can be measured"
    }
    Start-Sleep -Seconds $SettleSeconds

    $title = Get-WindowTitle $process.Id
    Add-Check "window" "the title names the first tab" ($title -eq "Rock Band Blitz Launcher - General") $title

    # One throwaway capture before anything is asserted. capture_window.ps1 restores the
    # window to read its pixels (`ShowWindow(h, 9)`), so the *first* capture of a leg is of
    # the maximized layout and every later one is of the restored window. On this machine
    # those two are the same size at the display's own scale and differ at a smaller one -
    # where the restored window is narrow enough to lose a hint from the help strip - so the
    # warm-up is what makes the idle pair below a pair of the same layout rather than a
    # measurement of that first restore. It is not an assertion: it exists to be thrown away.
    $null = Save-Frame "warmup"
    Start-Sleep -Milliseconds 400

    $first = Save-Frame "keyboard-00"
    Add-Check "window" "the launcher window can be captured" $first.Ok $first.Text    if (-not $first.Ok) {
        Stop-Launcher $process | Out-Null
        throw "no capture: the claims below are about pixels"
    }
    Start-Sleep -Milliseconds 800
    $second = Save-Frame "keyboard-01"
    Add-Check "window" "a second capture of the same state" $second.Ok $second.Text

    $size = Get-FrameSize $first.Path
    $region = Get-Region $uiScale $size.W $size.H
    Write-Host ("client capture {0}x{1}, ui scale {2}: bar {3}px, help {4}px, body from y={5}" -f `
            $size.W, $size.H, $region.Scale, $region.BarPixels, $region.Help.H, $region.Body.Y)

    # The control pair: nothing was pressed and nothing was drawn differently, so the ring
    # cannot have moved and the help line cannot have been rewritten. Without this a tab
    # switch that merely redrew everything would pass every check below.
    $idleHelp = Get-DiffPercent (Save-Crop $first.Path "help" $region.Help 0) `
                                (Save-Crop $second.Path "help" $region.Help 1)
    $idleBody = Get-DiffPercent (Save-Crop $first.Path "body" $region.Body 0) `
                                (Save-Crop $second.Path "body" $region.Body 1)
    Add-Check "window" "no input, no change in the help strip ($idleHelp% <= $NoisePercent%)" `
        ($idleHelp -ge 0 -and $idleHelp -le $NoisePercent) "$idleHelp%"
    Add-Check "window" "no input, no change in the body ($idleBody% <= $NoisePercent%)" `
        ($idleBody -ge 0 -and $idleBody -le $NoisePercent) "$idleBody%"
    $diffs.Add([pscustomobject]@{ Leg = "keyboard"; Pair = "idle help"; Percent = $idleHelp }) | Out-Null
    $diffs.Add([pscustomobject]@{ Leg = "keyboard"; Pair = "idle body"; Percent = $idleBody }) | Out-Null

    # One press, one focus entry. Down `k` times, a capture after each, and two crops per
    # press: the body (the ring's highlight is in it) and the help strip.
    #
    # "Rows" and "entries" are not the same thing, and the difference is why the assertions
    # below are trace-driven: a row that offers a choice - the launch target, with three -
    # owns one focus entry per choice (P0.2's choices_from, A1's ring), so a press inside
    # such a row moves the ring without moving to another row. The bar's help line is the
    # *row's* (A2), so those presses leave it saying exactly the same thing. What can be
    # asserted per press is therefore: the ring moved (the body changed), the trace counted
    # one entry, and - where the row changed - the help line was rewritten.
    $frames = @($first.Path)
    $walk = New-Object System.Collections.ArrayList
    for ($press = 1; $press -le $DownPresses; $press++) {
        Send-KeyToLauncher $process.Id "down"
        $frame = Save-Frame ("keyboard-{0:D2}" -f ($press + 1))
        Add-Check "window" "capture after press $press" $frame.Ok $frame.Text
        if (-not $frame.Ok) { break }
        $frames += $frame.Path
        $helpDiff = Get-DiffPercent (Save-Crop $frames[$press - 1] "help" $region.Help ($press - 1)) `
                                    (Save-Crop $frames[$press] "help" $region.Help $press)
        $bodyDiff = Get-DiffPercent (Save-Crop $frames[$press - 1] "body" $region.Body ($press - 1 + 10)) `
                                    (Save-Crop $frames[$press] "body" $region.Body ($press + 10))
        $diffs.Add([pscustomobject]@{ Leg = "keyboard"; Pair = "down $press help"; Percent = $helpDiff }) | Out-Null
        $diffs.Add([pscustomobject]@{ Leg = "keyboard"; Pair = "down $press body"; Percent = $bodyDiff }) | Out-Null
        $walk.Add([pscustomobject]@{
            Press = $press; Help = $helpDiff; Body = $bodyDiff; Entry = -1; Row = ""; RowChanged = $false
        }) | Out-Null
    }
    if ($walk.Count -ne $DownPresses) {
        Add-Check "window" "every press was captured" $false "$($walk.Count) of $DownPresses"
    }

    $trace = (Get-FocusTrace $focusLog).Lines
    $entries = @($trace | ForEach-Object { $_.Entry })
    $tabs = @($trace | ForEach-Object { $_.Tab } | Select-Object -Unique)
    Add-Check "window" "the trace names one tab for the whole walk" ($tabs.Count -eq 1) ($tabs -join ",")
    Add-Check "window" "the trace has one line per press plus the start" `
        ($entries.Count -eq ($walk.Count + 1)) "entries: $($entries -join ',')"
    $movesByOne = $entries.Count -ge 2
    for ($i = 1; $i -lt $entries.Count; $i++) {
        if ($entries[$i] -ne ($entries[$i - 1] + 1)) { $movesByOne = $false }
    }
    Add-Check "window" "one press moves the ring one entry" $movesByOne "entries: $($entries -join ',')"

    # The trace is what says which of those presses left the row it was on, so the walk rows
    # are filled in from it before the pixel claims are made.
    for ($i = 0; $i -lt $walk.Count; $i++) {
        if (($i + 1) -ge $trace.Count) { break }
        $walk[$i].Entry = $trace[$i + 1].Entry
        $walk[$i].Row = $trace[$i + 1].Row
        $walk[$i].RowChanged = ($trace[$i + 1].Row -ne $trace[$i].Row)
    }
    $rowMoves = @($walk | Where-Object { $_.RowChanged })
    Add-Check "window" "the walk crossed at least one row boundary" ($rowMoves.Count -ge 1) `
        "$($rowMoves.Count) of $($walk.Count) presses"
    Add-Check "window" "the ring stopped below the last entry" `
        ($entries.Count -ge 2 -and $entries[$entries.Count - 1] -lt $trace[$trace.Count - 1].Count) `
        "entry $($entries[$entries.Count - 1])/$($trace[$trace.Count - 1].Count)"
    $bodyFloor = if ($walk.Count -gt 0) { ($walk | Measure-Object -Property Body -Minimum).Minimum } else { -1 }
    Add-Check "window" "every press moves the body ($bodyFloor% >= $PressBodyPercent%)" `
        ($walk.Count -gt 0 -and $bodyFloor -ge $PressBodyPercent) "smallest: $bodyFloor%"
    $helpFloor = if ($rowMoves.Count -gt 0) { ($rowMoves | Measure-Object -Property Help -Minimum).Minimum } else { -1 }
    Add-Check "window" "a press that changes the row rewrites the help line ($helpFloor% >= $HelpChangePercent%)" `
        ($rowMoves.Count -ge 1 -and $helpFloor -ge $HelpChangePercent) "smallest: $helpFloor%"
    # The presses that stayed on one row are reported rather than asserted on. The launch
    # target's three choices are three entries and one row, so they share one help sentence
    # and move 0% of the strip; the settings-file block's five entries are also one row by
    # the trace's naming, but each has its own sentence, so those move it. "One row, one
    # help line" is therefore not a promise - which is why the assertion above is on the
    # presses that changed the row, and the walk's own numbers are in summary.json.
    Add-Check "window" "the ring stopped below the last row" `
        ($entries.Count -ge 2 -and $entries[$entries.Count - 1] -lt $trace[$trace.Count - 1].Count) `
        "entry $($entries[$entries.Count - 1])/$($trace[$trace.Count - 1].Count)"

    # The tab switch: the title is the launcher's own report of the selected tab (A1), and
    # the body is where the change has to be visible. The strip and the bar are outside the
    # body crop, so a difference in it is the tab's own content and not the highlighted
    # button that says which tab is selected.
    $lastFrame = $frames[$frames.Count - 1]
    Send-KeyToLauncher $process.Id "right"
    $tabFrame = Save-Frame "keyboard-tab"
    $titleAfter = Get-WindowTitle $process.Id
    Add-Check "window" "Right moves to the next tab" ($titleAfter -eq "Rock Band Blitz Launcher - Audio / Video") $titleAfter
    $bodyDiff = Get-DiffPercent (Save-Crop $lastFrame "body" $region.Body 90) `
                                (Save-Crop $tabFrame.Path "body" $region.Body 91)
    $diffs.Add([pscustomobject]@{ Leg = "keyboard"; Pair = "tab switch body"; Percent = $bodyDiff }) | Out-Null
    Add-Check "window" "the tab switch changes the body ($bodyDiff% >= $BodyChangePercent%)" `
        ($bodyDiff -ge $BodyChangePercent) "$bodyDiff%"

    # ...and back, which is the same tab and not merely another one: the body is closer to
    # the frame the walk ended on than the tab we just left was.
    Send-KeyToLauncher $process.Id "left"
    $backFrame = Save-Frame "keyboard-back"
    $titleBack = Get-WindowTitle $process.Id
    Add-Check "window" "Left comes back to the first tab" ($titleBack -eq "Rock Band Blitz Launcher - General") $titleBack
    $backDiff = Get-DiffPercent (Save-Crop $lastFrame "body" $region.Body 92) `
                                (Save-Crop $backFrame.Path "body" $region.Body 93)
    $diffs.Add([pscustomobject]@{ Leg = "keyboard"; Pair = "back body"; Percent = $backDiff }) | Out-Null
    Add-Check "window" "the tab we came back to is the one we left ($backDiff% < $bodyDiff%)" `
        ($backDiff -ge 0 -and $backDiff -lt $bodyDiff) "$backDiff% vs $bodyDiff%"

    $clean = Stop-Launcher $process
    Add-Check "window" "Escape leaves the launcher" $clean ""
    # A1 keeps the window's size without being asked, and writes it from the profile as the
    # file last had it - so a run that pressed nothing but Escape may rewrite the two
    # [window] numbers and nothing else. (It writes them here because capture_window.ps1
    # restores the window to read its pixels, and a restored window is no longer maximized.)
    # What must *not* appear is a [settings] table, a changed target, or any other row: that
    # is what Save is for, and nothing here pressed it.
    $after = if (Test-Path -LiteralPath $ProfilePath) { (Get-Content -LiteralPath $ProfilePath -Raw) } else { "" }
    $normalized = (ConvertTo-NormalizedProfile $after)
    Add-Check "window" "the run wrote nothing but the window size" `
        ($normalized -eq $fixtureText -and $after -notmatch "\[settings\]") `
        ("geometry now: " + (Get-ProfileGeometry $after) + " (the fixture was " + (Get-ProfileGeometry $fixtureRaw) + ")")

    $keyboard = [pscustomobject]@{ Trace = $trace; Title = $title; Size = "$($size.W)x$($size.H)"; UiScale = $uiScale }
}

# --------------------------------------------------------------- the pad leg ---

if (-not $SkipWindow -and -not $SkipPadLeg -and $PadScript) {
    $padLog = Join-Path $capDir "pad-focus.log"
    if (Test-Path $padLog) { Remove-Item $padLog -Force }
    $arguments = @(
        "--launcher_profile=$ProfilePath", "--game_data_root=$GameRoot",
        "--focus-log=$padLog", "--test-pad=$PadScript"
    )
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
    $hwnd = Get-LauncherWindow $process.Id $WindowTimeoutSec
    if ($hwnd -eq [IntPtr]::Zero) {
        Add-Check "pad" "the launcher opens for the pad leg" $false "pid $($process.Id)"
        Stop-Launcher $process | Out-Null
    } else {
        Add-Check "pad" "the launcher opens for the pad leg" $true ""
        # The pad's own schedule: a step is held for its length and the next begins 400 ms
        # after it is let go of (launcher/src/virtual_pad.cpp), and the pad arrives at the
        # start - so the wait is the script's length plus room for the window to be up.
        Start-Sleep -Seconds ([Math]::Max(4, $SettleSeconds + 4))
        $padTrace = (Get-FocusTrace $padLog).Lines
        $padText = if (Test-Path $padLog) { (Get-Content -LiteralPath $padLog -Raw) } else { "" }
        Add-Check "pad" "the scripted pad announces itself" `
            ($padText -match 'device gamepad name="Virtual Pad"') `
            (($padText -split "`n" | Where-Object { $_ -match "device gamepad" }) -join " ").Trim()
        $padEntries = @($padTrace | ForEach-Object { $_.Entry })
        Add-Check "pad" "the pad moves the ring without a key ($($padEntries.Count - 1) move(s))" `
            ($padEntries.Count -ge 3) "entries: $($padEntries -join ',')"
        $padTabs = @($padTrace | ForEach-Object { $_.Tab } | Select-Object -Unique)
        Add-Check "pad" "the pad drives the same ring on one tab" ($padTabs.Count -eq 1) ($padTabs -join ",")
        $clean = Stop-Launcher $process
        Add-Check "pad" "Escape leaves the pad leg's launcher" $clean ""
        $pad = [pscustomobject]@{ Script = $PadScript; Trace = $padTrace }
    }
}

# ---------------------------------------------------- the A5 legs (walking, keys, DPI) ---

# The walkthrough: a lap of every tab's ring with the keyboard alone, which is A5's "complete
# every tab with the keyboard only" and its "never traps focus" in one measurement. No crop
# is taken, because the launcher's own trace is the better oracle here: it names the entry
# the ring is on and how many there are, so a lap either visited all of them or it did not.
$walkthrough = $null
if (-not $SkipWindow -and -not $SkipWalkthrough) {
    $walkLog = Join-Path $capDir "walkthrough-focus.log"
    if (Test-Path $walkLog) { Remove-Item $walkLog -Force }
    $leg = Start-Launcher @(
        "--launcher_profile=$ProfilePath", "--game_data_root=$GameRoot",
        "--focus-log=$walkLog", "--no-gamepad"
    )
    if ($leg.Window -eq [IntPtr]::Zero) {
        Add-Check "walkthrough" "the launcher opens for the walkthrough" $false "pid $($leg.Process.Id)"
        Stop-Launcher $leg.Process | Out-Null
    } else {
        Add-Check "walkthrough" "the launcher opens for the walkthrough" $true ""
        Start-Sleep -Seconds $SettleSeconds
        # A lap per tab, driven blind and read once at the end. The leg deliberately does *not*
        # count entries as it goes: the log is a file, and reading it between presses is how a
        # harness ends up measuring its own timing rather than the launcher's. So each tab gets
        # Home and then a fixed budget of Downs - more than any ring here has - and the analysis
        # below asks the one question that matters of the whole run at once: did the ring visit
        # every entry the tab says it has, and come back round to the first one?
        $tabs = @(
            @{ Name = "General"; Key = "general" }
            @{ Name = "Audio / Video"; Key = "graphics" }
            @{ Name = "Controller"; Key = "controller" }
        )
        foreach ($tab in $tabs) {
            $title = Get-WindowTitle $leg.Process.Id
            Add-Check "walkthrough" "the walk starts on $($tab.Name)" `
                ($title -eq "Rock Band Blitz Launcher - $($tab.Name)") $title
            # Home first, so a lap starts at entry 0 whatever the tab was left on, and so the
            # wrap below is a wrap rather than "it happened to be near the end".
            Send-KeyToLauncher $leg.Process.Id "home"
            for ($press = 0; $press -lt $LapPresses; $press++) {
                Send-KeyToLauncher $leg.Process.Id "down"
            }
            Send-KeyToLauncher $leg.Process.Id "right"
        }
        Start-Sleep -Milliseconds 500
        $afterWalk = Get-WindowTitle $leg.Process.Id
        Add-Check "walkthrough" "Right wrapped from the last tab to the first" `
            ($afterWalk -eq "Rock Band Blitz Launcher - General") $afterWalk
        $clean = Stop-Launcher $leg.Process
        Add-Check "walkthrough" "Escape still leaves after the walk" $clean ""

        # One read, then everything the walk was for. The lines are grouped into runs of one tab,
        # which is what the three tab switches make them.
        $lines = @((Get-FocusTrace $walkLog).Lines)
        Add-Check "walkthrough" "the walk left a trace" ($lines.Count -gt ($tabs.Count * 2)) `
            "$($lines.Count) line(s)"
        for ($index = 0; $index -lt $tabs.Count; $index++) {
            $tab = $tabs[$index]
            $run = New-Object System.Collections.ArrayList
            $started = $false
            foreach ($line in $lines) {
                if ($line.Tab -eq $tab.Key) {
                    $started = $true
                    $run.Add($line) | Out-Null
                } elseif ($started) {
                    # A run ends at the first line of the next tab, and never resumes: the walk
                    # visits each tab once.
                    break
                }
            }
            $entries = @($run | ForEach-Object { $_.Entry })
            $count = if ($run.Count -gt 0) { $run[0].Count } else { 0 }
            Add-Check "walkthrough" "$($tab.Name) has a ring to walk ($count entries)" `
                ($count -ge 2) "$count entries"
            # Every entry the tab claims, visited: a ring the keyboard cannot reach the end of
            # would leave a hole in this set.
            $missing = @()
            for ($entry = 0; $entry -lt $count; $entry++) {
                if ($entries -notcontains $entry) { $missing += $entry }
            }
            Add-Check "walkthrough" "a lap of $($tab.Name) reaches all $count entries" `
                ($missing.Count -eq 0) ("missing: " + ($missing -join ","))
            # ...and it wraps: an entry 0 that follows the last entry is a ring, not a wall.
            $wrapped = $false
            for ($i = 1; $i -lt $entries.Count; $i++) {
                if ($entries[$i] -eq 0 -and $entries[$i - 1] -eq ($count - 1)) { $wrapped = $true }
            }
            Add-Check "walkthrough" "a lap of $($tab.Name) comes back round to the first entry" `
                $wrapped "entries: $($entries -join ',')"
            $ringTabs = @($run | ForEach-Object { $_.Tab } | Select-Object -Unique)
            Add-Check "walkthrough" "the lap stays on one tab" ($ringTabs.Count -eq 1) ($ringTabs -join ",")
            $rows = @($run | ForEach-Object { $_.Row } | Where-Object { $_ } | Select-Object -Unique)
            $distinct = @($entries | Select-Object -Unique)
            $laps.Add([pscustomobject]@{
                Tab = $tab.Name; Entries = $count; Visited = $distinct.Count; Rows = $rows
            }) | Out-Null
            # The two blocks the General tab draws after its rows have to be in the ring: a lap
            # that never named them would mean they are drawn outside it, which is the one way
            # this tab's settings could become keyboard-unreachable.
            if ($tab.Key -eq "general") {
                Add-Check "walkthrough" "the lap reaches the launcher-keys block" `
                    (@($rows | Where-Object { $_ -match "^launcher-key:" }).Count -gt 0) ($rows -join ",")
                Add-Check "walkthrough" "the lap reaches the settings-file block" `
                    ($rows -contains "settings-file-block") ($rows -join ",")
            }
        }
        $walkthrough = [pscustomobject]@{ Laps = $laps.ToArray(); Lines = $lines }
    }
}

# The rebinding leg: bind navigation to an unused key, save it with the key A5 binds to Save,
# and put it back. Everything it asserts is read out of the profile on disk or out of the
# trace, so "the binding arrived" and "the binding came back" are file facts rather than
# screen facts.
$keyBindings = $null
if (-not $SkipWindow -and -not $SkipKeys) {
    $keysDir = Join-Path $capDir "keys"
    $keysProfile = Join-Path $keysDir "launcher.toml"
    Write-Fixture $keysProfile $Target
    $keysLog = Join-Path $keysDir "focus.log"
    if (Test-Path $keysLog) { Remove-Item $keysLog -Force }
    $leg = Start-Launcher @(
        "--launcher_profile=$keysProfile", "--game_data_root=$GameRoot",
        "--focus-log=$keysLog", "--no-gamepad"
    )
    if ($leg.Window -eq [IntPtr]::Zero) {
        Add-Check "keys" "the launcher opens for the rebinding leg" $false "pid $($leg.Process.Id)"
        Stop-Launcher $leg.Process | Out-Null
    } else {
        Add-Check "keys" "the launcher opens for the rebinding leg" $true ""
        Start-Sleep -Seconds $SettleSeconds
        # Walk to the first action's Assign by watching the trace name that row rather than
        # counting entries here: the count is the tab's business, and a second copy of it in the
        # harness would be the thing that breaks when the block grows.
        $reached = Step-ToKeyRow $leg.Process.Id $keysLog "next:assign"
        Add-Check "keys" "the ring reaches the first action's Assign row" $reached (Get-FocusedKeyRow $keysLog)

        if ($reached) {
            $trace = (Get-FocusTrace $keysLog).Lines
            $assignEntry = $trace[$trace.Count - 1].Entry
            $ringCount = $trace[$trace.Count - 1].Count

            # Assign, then press J: the capture takes the first key it sees, and J is a key no
            # default binding names. Enter and J are both sent, and the *ring must not move* in
            # between: while a capture is running the key being pressed is the input being
            # captured, not a command.
            Send-KeyToLauncher $leg.Process.Id "enter"
            Send-KeyToLauncher $leg.Process.Id "j"
            Start-Sleep -Milliseconds 600
            $afterCapture = (Get-FocusTrace $keysLog).Lines
            Add-Check "keys" "a capture does not let the key it captures move the ring" `
                ($afterCapture[$afterCapture.Count - 1].Entry -eq $assignEntry) `
                "entry $($afterCapture[$afterCapture.Count - 1].Entry) of $ringCount"
            # The binding is live: the launcher rebuilds the table from the profile every frame,
            # so J has to move the ring without a restart. It is also what says the capture has
            # ended - a capture still running would swallow the press.
            $beforeJ = (Get-FocusTrace $keysLog).Lines.Count
            Send-KeyToLauncher $leg.Process.Id "j"
            $afterJ = (Get-FocusTrace $keysLog).Lines
            Add-Check "keys" "the key that was just assigned moves the ring" `
                ($afterJ.Count -gt $beforeJ) "$beforeJ -> $($afterJ.Count) trace line(s)"
            # Save with the key A5 binds to the bar's Save button.
            Send-KeyToLauncher $leg.Process.Id "ctrl+s"
            Start-Sleep -Milliseconds 400
            $saved = if (Test-Path -LiteralPath $keysProfile) { Get-Content -LiteralPath $keysProfile -Raw } else { "" }
            Add-Check "keys" "Ctrl+S writes the profile" ($saved -match '(?m)^\[nav\]') ""
            Add-Check "keys" "the assigned key is in the [nav] table" `
                ($saved -match '(?m)^next = "Tab, DownArrow, J"') `
                (($saved -split "`n" | Where-Object { $_ -match "^next" }) -join " ").Trim()

            # ...and back: that action's Reset row returns it to the two keys it ships with. The
            # ring is on the Assign row when this starts - J moved it a row - so the walk is by
            # name again rather than "one press down". Save afterwards and the file has no `next`
            # row at all, because a profile records only what differs from the defaults.
            $atReset = Step-ToKeyRow $leg.Process.Id $keysLog "next:reset"
            Add-Check "keys" "the ring reaches that action's Reset row" $atReset (Get-FocusedKeyRow $keysLog)
            Send-KeyToLauncher $leg.Process.Id "enter"
            Start-Sleep -Milliseconds 300
            Send-KeyToLauncher $leg.Process.Id "ctrl+s"
            Start-Sleep -Milliseconds 400
            $reset = if (Test-Path -LiteralPath $keysProfile) { Get-Content -LiteralPath $keysProfile -Raw } else { "" }
            Add-Check "keys" "Reset takes the action back to the keys it ships with" `
                ($reset -notmatch '(?m)^next = ') `
                (($reset -split "`n" | Where-Object { $_ -match "^next" }) -join " ").Trim()
            # The ring answers Tab again, which is what "back" means: the trace moved on a Tab
            # press and the profile says nothing about `next`.
            $beforeTab = (Get-FocusTrace $keysLog).Lines.Count
            Send-KeyToLauncher $leg.Process.Id "tab"
            $afterTab = (Get-FocusTrace $keysLog).Lines
            Add-Check "keys" "the default key works again after the reset" `
                ($afterTab.Count -gt $beforeTab) "$beforeTab -> $($afterTab.Count) trace line(s)"
            $clean = Stop-Launcher $leg.Process
            Add-Check "keys" "Escape leaves the rebinding leg's launcher" $clean ""
        } else {
            Stop-Launcher $leg.Process | Out-Null
        }
        $keyBindings = [pscustomobject]@{
            Profile = $keysProfile; Trace = (Get-FocusTrace $keysLog).Lines
            After = if (Test-Path -LiteralPath $keysProfile) { Get-Content -LiteralPath $keysProfile -Raw } else { "" }
        }
    }
}

# The scale leg: the same two claims the keyboard leg above makes, made at three more UI
# scales. The machine's own scale is the fourth step (the keyboard leg is that one), and the
# claim is not "the picture is identical" - it cannot be, the UI is a different size - but
# that the crops the harness computes out of the scale still contain what they are meant to:
# with no input the pair is identical, one press moves the ring's own region, and a press
# that crosses a row rewrites the help strip. A ring or a bar that did not scale would put
# the crops in the wrong place at one of these steps and fail one of the three.
$scales = $null
if (-not $SkipWindow -and -not $SkipScale) {
    $scaleReport = New-Object System.Collections.ArrayList
    foreach ($scale in $UiScales) {
        $scaleDir = Join-Path $capDir ("scale-" + $scale)
        $scaleProfile = Join-Path $scaleDir "launcher.toml"
        Write-Fixture $scaleProfile $Target
        $scaleLog = Join-Path $scaleDir "focus.log"
        if (Test-Path $scaleLog) { Remove-Item $scaleLog -Force }
        $leg = Start-Launcher @(
            "--launcher_profile=$scaleProfile", "--game_data_root=$GameRoot",
            "--focus-log=$scaleLog", "--no-gamepad", "--ui-scale=$scale"
        )
        if ($leg.Window -eq [IntPtr]::Zero) {
            Add-Check "scale $scale" "the launcher opens at this scale" $false "pid $($leg.Process.Id)"
            Stop-Launcher $leg.Process | Out-Null
            continue
        }
        Start-Sleep -Seconds $SettleSeconds
        # The same throwaway capture the keyboard leg takes, and for the same reason: the first
        # capture of a leg restores the window, and this leg exists to measure layouts that a
        # restored window can differ from.
        $null = Save-Frame ("scale-" + $scale + "-warmup")
        Start-Sleep -Milliseconds 400
        $first = Save-Frame ("scale-" + $scale + "-00")
        Start-Sleep -Milliseconds 800
        $second = Save-Frame ("scale-" + $scale + "-01")
        $size = Get-FrameSize $first.Path
        $region = Get-Region $scale $size.W $size.H
        $idle = Get-DiffPercent (Save-Crop $first.Path "help" $region.Help 200) `
                                (Save-Crop $second.Path "help" $region.Help 201)
        Add-Check "scale $scale" "the crops land on a still picture at this scale ($idle% <= $NoisePercent%)" `
            ($idle -ge 0 -and $idle -le $NoisePercent) "$idle%"
        # Four presses, which is enough to cross a row on any tab whose first row is the launch
        # target's three choices: the fourth press leaves it.
        $frames = @($second.Path)
        $bodyMin = -1.0
        $helpMax = -1.0
        for ($press = 1; $press -le 4; $press++) {
            Send-KeyToLauncher $leg.Process.Id "down"
            $frame = Save-Frame ("scale-" + $scale + "-" + ("{0:D2}" -f ($press + 1)))
            if (-not $frame.Ok) { break }
            $body = Get-DiffPercent (Save-Crop $frames[$press - 1] "body" $region.Body (300 + $press)) `
                                    (Save-Crop $frame.Path "body" $region.Body (400 + $press))
            $help = Get-DiffPercent (Save-Crop $frames[$press - 1] "help" $region.Help (300 + $press)) `
                                    (Save-Crop $frame.Path "help" $region.Help (400 + $press))
            if ($bodyMin -lt 0 -or $body -lt $bodyMin) { $bodyMin = $body }
            if ($help -gt $helpMax) { $helpMax = $help }
            $frames += $frame.Path
        }
        Add-Check "scale $scale" "a press moves the ring's own region ($bodyMin% >= $PressBodyPercent%)" `
            ($bodyMin -ge $PressBodyPercent) "smallest: $bodyMin%"
        Add-Check "scale $scale" "leaving the first row rewrites the help strip ($helpMax% >= $HelpChangePercent%)" `
            ($helpMax -ge $HelpChangePercent) "largest: $helpMax%"
        $clean = Stop-Launcher $leg.Process
        Add-Check "scale $scale" "Escape leaves at this scale" $clean ""
        $scaleReport.Add([pscustomobject]@{
            Scale = $scale; UiScale = $uiScale; ClientSize = "$($size.W)x$($size.H)"
            BarPixels = $region.BarPixels; HelpPixels = $region.Help.H
            Idle = $idle; PressBodyMin = $bodyMin; RowHelpMax = $helpMax
        }) | Out-Null
    }
    # And the switch the leg is built on is the switch the launcher reports: --dump-display
    # says which scale it used and where that number came from.
    $scaled = Invoke-Dump "dump-display"
    Add-Check "scale" "--dump-display names the scale's source" `
        ($scaled.Text -match "scale source\s+:\s+the display's content scale") ""
    $scales = $scaleReport.ToArray()
}

# The safe-mode leg: a profile that has taken every movement key away, which is the one way a
# launcher can be made unusable by its own settings file. Without the switch, nothing but
# Escape works; with it, the launcher starts on the defaults - and writes nothing, so the
# file the user is about to repair is exactly as they left it.
$safeMode = $null
if (-not $SkipSafeMode) {
    $lockedDir = Join-Path $capDir "locked-out"
    $lockedProfile = Join-Path $lockedDir "launcher.toml"
    $lockedNav = @"
[nav]
next = ""
previous = ""
first = ""
last = ""
next_tab = ""
previous_tab = ""
"@
    Write-Fixture $lockedProfile $Target $lockedNav
    $lockedBefore = Get-Content -LiteralPath $lockedProfile -Raw

    # Headless: what the launcher says about the file, with and without the switch.
    $strictDump = Join-Path $capDir "locked-strict.txt"
    $safeDump = Join-Path $capDir "locked-safe.txt"
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList @(
        "--dump-profile=$strictDump", "--launcher_profile=$lockedProfile")
    $strictText = if (Test-Path $strictDump) { Get-Content -LiteralPath $strictDump -Raw } else { "" }
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList @(
        "--dump-profile=$safeDump", "--launcher_profile=$lockedProfile", "--safe-mode")
    $safeText = if (Test-Path $safeDump) { Get-Content -LiteralPath $safeDump -Raw } else { "" }
    Add-Check "safe-mode" "the readable profile stays writable without the switch" `
        ($strictText -match "writable\s+: yes") ""
    Add-Check "safe-mode" "the switch says what it did" `
        ($safeText -match "safe mode\s+: yes") `
        (($safeText -split "`n" | Where-Object { $_ -match "safe mode" }) -join " ").Trim()

    # A file that does not parse at all: refused without the switch (D2's rule), replaceable
    # with it, which is the recovery for a user who has no copy of the file to import.
    $brokenDir = Join-Path $capDir "broken"
    New-Item -ItemType Directory -Force -Path $brokenDir | Out-Null
    $brokenProfile = Join-Path $brokenDir "launcher.toml"
    Set-Content -LiteralPath $brokenProfile -Value 'schema_version = "one"' -NoNewline -Encoding ascii
    $brokenStrict = Join-Path $capDir "broken-strict.txt"
    $brokenSafe = Join-Path $capDir "broken-safe.txt"
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList @(
        "--dump-profile=$brokenStrict", "--launcher_profile=$brokenProfile")
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList @(
        "--dump-profile=$brokenSafe", "--launcher_profile=$brokenProfile", "--safe-mode")
    $brokenStrictText = if (Test-Path $brokenStrict) { Get-Content -LiteralPath $brokenStrict -Raw } else { "" }
    $brokenSafeText = if (Test-Path $brokenSafe) { Get-Content -LiteralPath $brokenSafe -Raw } else { "" }
    Add-Check "safe-mode" "a file that did not parse is refused without the switch" `
        ($brokenStrictText -match "writable\s+: no") ""
    Add-Check "safe-mode" "the switch offers to replace it instead" `
        ($brokenSafeText -match "writable\s+: yes" -and $brokenSafeText -match "will replace it") ""
    Add-Check "safe-mode" "nothing was written while only looking" `
        ((Get-Content -LiteralPath $brokenProfile -Raw) -eq 'schema_version = "one"') ""

    if (-not $SkipWindow) {
        # The locked-out profile in the window: without the switch the ring cannot be moved at
        # all - which is the trap - and with it every default key works again.
        $lockedLog = Join-Path $lockedDir "focus.log"
        if (Test-Path $lockedLog) { Remove-Item $lockedLog -Force }
        $leg = Start-Launcher @(
            "--launcher_profile=$lockedProfile", "--game_data_root=$GameRoot",
            "--focus-log=$lockedLog", "--no-gamepad"
        )
        if ($leg.Window -eq [IntPtr]::Zero) {
            Add-Check "safe-mode" "the launcher opens with a locked-out profile" $false "pid $($leg.Process.Id)"
            Stop-Launcher $leg.Process | Out-Null
        } else {
            Add-Check "safe-mode" "the launcher opens with a locked-out profile" $true ""
            Start-Sleep -Seconds $SettleSeconds
            $before = (Wait-FocusTrace $lockedLog).Lines.Count
            for ($press = 0; $press -lt 3; $press++) { Send-KeyToLauncher $leg.Process.Id "down" }
            $afterLocked = (Get-FocusTrace $lockedLog).Lines.Count
            Add-Check "safe-mode" "without the switch no key moves the ring" `
                ($afterLocked -eq $before) "$before -> $afterLocked trace line(s)"
            $clean = Stop-Launcher $leg.Process
            Add-Check "safe-mode" "Escape still leaves the locked-out launcher" $clean ""
            # The run wrote the window's size, which is A1's own rule and not this leg's
            # business: what matters is that nothing in [nav] changed.
            $lockedAfter = Get-Content -LiteralPath $lockedProfile -Raw
            Add-Check "safe-mode" "nothing rewrote the keys" `
                (($lockedAfter -match '(?m)^next = ""') -and ($lockedAfter -match '(?m)^previous = ""')) ""

            $safeLog = Join-Path $lockedDir "safe-focus.log"
            if (Test-Path $safeLog) { Remove-Item $safeLog -Force }
            $leg = Start-Launcher @(
                "--launcher_profile=$lockedProfile", "--game_data_root=$GameRoot",
                "--focus-log=$safeLog", "--no-gamepad", "--safe-mode"
            )
            Start-Sleep -Seconds $SettleSeconds
            $before = (Wait-FocusTrace $safeLog).Lines.Count
            for ($press = 0; $press -lt 3; $press++) { Send-KeyToLauncher $leg.Process.Id "down" }
            $afterSafe = (Get-FocusTrace $safeLog).Lines.Count
            Add-Check "safe-mode" "the switch starts on the defaults, so Down moves the ring" `
                ($afterSafe -gt $before) "$before -> $afterSafe trace line(s)"
            $clean = Stop-Launcher $leg.Process
            Add-Check "safe-mode" "Escape leaves the safe-mode launcher" $clean ""
            # The promise that makes the switch safe to use on a file worth repairing: safe
            # mode writes nothing, not even the window size it opened at.
            Add-Check "safe-mode" "safe mode left the file byte for byte as it was" `
                ((Get-Content -LiteralPath $lockedProfile -Raw) -eq $lockedBefore) `
                ("now: " + (Get-ProfileGeometry (Get-Content -LiteralPath $lockedProfile -Raw)) +
                 " (was " + (Get-ProfileGeometry $lockedBefore) + ")")
            $safeMode = [pscustomobject]@{
                Profile = $lockedProfile; LockedTrace = (Get-FocusTrace $lockedLog).Lines
                SafeTrace = (Get-FocusTrace $safeLog).Lines; BrokenStrict = $brokenStrictText
                BrokenSafe = $brokenSafeText
            }
        }
    }
}

# --------------------------------------------------------------------- report ---
$failed = @($checks | Where-Object { -not $_.Ok })
Write-Host "`n=== checks ==="
$checks | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
if ($diffs.Count -ne 0) {
    Write-Host "=== frame differences ==="
    $diffs | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
}
Write-Host ("{0} of {1} checks passed" -f ($checks.Count - $failed.Count), $checks.Count)
foreach ($f in $failed) { Write-Host "FAILED [$($f.Leg)] $($f.Check) - $($f.Detail)" }

[pscustomobject]@{
    Exe = $exe; Profile = $ProfilePath; Target = $Target; GameRoot = $GameRoot
    UiScale = $uiScale; DownPresses = $DownPresses; CropScale = $CropScale
    Keyboard = $keyboard; Pad = $pad; Walk = $walk.ToArray()
    Walkthrough = $walkthrough; KeyBindings = $keyBindings; Scales = $scales; SafeMode = $safeMode
    Diffs = $diffs.ToArray(); Checks = $checks.ToArray()
} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $capDir "summary.json")

if ($failed.Count -ne 0) { exit 1 }
exit 0
