# Drive the rb_blitz window with keystrokes and capture screenshots, for
# bring-up verification (input mapping, menu navigation).
#
# The game window must be foreground before a key is sent, so every "key:"
# action re-asserts the foreground window first. Keys are sent through the
# MnK controller emulation (cvar mnk_mode = true), so "a"/"b"/"up"/"start"
# etc. map onto the emulated pad buttons via the SDK keybinds.
#
# Usage:
#   .\scripts\drive_ui.ps1 -Actions "wait:26","shot:title.png","key:a","wait:5","shot:menu.png"
#
# Action grammar:
#   wait:<seconds>      sleep
#   shot:<file.png>     capture the game window (relative to out/drive-ui/)
#   key:<name>          focus the window, then send one key
#   hold:<name>:<secs>  focus the window, hold a key down for <secs>
#   refocus             minimize then restore the window (forces an SDL
#                       focus-lost/focus-gained pair; the MnK driver only
#                       accepts keys while it believes it has focus)
#   mouse:<x>:<y>      move to normalized game-client coordinates (0..1)
#   click:left|right   click at the current pointer position
#   wheel:<detents>    scroll (positive up, negative down)
#   cursor:visible|hidden  assert the native cursor visibility
param(
    [Parameter(Mandatory = $true)][string[]]$Actions,
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/drive-ui"
)

$ErrorActionPreference = "Stop"
# The type survives between runs in a persistent shell session, and Add-Type
# fails when it is already loaded, so only define it once per session.
if (-not ("RbDriveWin32" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class RbDriveWin32 {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder text, int n);
    public static IntPtr GameWindow(int pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, p) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if (owner == pid) {
                var name = new System.Text.StringBuilder(128); GetClassName(h, name, 128);
                if (name.ToString() == "SDL_app") { found = h; return false; }
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    public struct Point { public int x, y; }
    public struct Rect { public int left, top, right, bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, int data, IntPtr extra);
    public struct CursorInfo { public int size, flags; public IntPtr cursor; public Point point; }
    [DllImport("user32.dll")] public static extern bool GetCursorInfo(ref CursorInfo info);
}
"@
}

$root = Split-Path -Parent $PSScriptRoot
$capDir = Join-Path $root $OutDir
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

# `powershell -File` hands the whole `-Actions a,b,c` list over as one
# comma-joined string, so split it back into separate actions.
if ($Actions.Count -eq 1 -and $Actions[0].Contains(",")) { $Actions = $Actions[0] -split "," }

# MnK keybind names -> SendKeys tokens (see mnk_input_driver.cpp defaults:
# A = Semicolon/Space, B = Quote/Backspace, X = L, Y = P, Start = X/Return,
# Back = Z/Tab, D-pad = Shift+arrows, sticks = WASD / arrows).
$keyMap = @{
    "a"        = " "
    "b"        = "{BACKSPACE}"
    "x"        = "l"
    "y"        = "p"
    "start"    = "{ENTER}"
    "back"     = "z"
    "esc"      = "{ESC}"
    "up"       = "{UP}"
    "down"     = "{DOWN}"
    "left"     = "{LEFT}"
    "right"    = "{RIGHT}"
    "dpad_up"  = "+{UP}"
    "dpad_down"= "+{DOWN}"
    "dpad_left"= "+{LEFT}"
    "dpad_right"= "+{RIGHT}"
    "lstick_up"= "w"
    "lstick_down"= "s"
    "lstick_left"= "a"
    "lstick_right"= "d"
}

# MnK keybind names -> Windows virtual-key codes. Defaults come from
# mnk_input_driver.cpp: A = Semicolon/Space, B = Quote/Backspace, X = L,
# Y = P, Start = Return, Back = Tab, D-pad = Shift+arrows, sticks = WASD /
# arrows, shoulders = 1 / 3, triggers = Q,I / E,O.
$vkMap = @{
    "a"            = 0x20   # Space
    "b"            = 0x08   # Backspace
    "x"            = 0x4C   # L
    "y"            = 0x50   # P
    "start"        = 0x0D   # Return
    "back"         = 0x09   # Tab
    "esc"          = 0x1B
    "up"           = 0x26
    "down"         = 0x28
    "left"         = 0x25
    "right"        = 0x27
    "dpad_up"      = 0x26
    "dpad_down"    = 0x28
    "dpad_left"    = 0x25
    "dpad_right"   = 0x27
    "lstick_up"    = 0x57   # W
    "lstick_down"  = 0x53   # S
    "lstick_left"  = 0x41   # A
    "lstick_right" = 0x44   # D
    "lstick_press" = 0x46   # F
    "rstick_up"    = 0x26
    "rstick_down"  = 0x28
    "rstick_left"  = 0x25
    "rstick_right" = 0x27
    "shoulder_l"   = 0x31   # 1
    "shoulder_r"   = 0x33   # 3
    "trigger_l"    = 0x51   # Q
    "trigger_r"    = 0x45   # E
}
$scMap = @{
    0x20 = 0x39; 0x08 = 0x0E; 0x4C = 0x26; 0x50 = 0x19; 0x0D = 0x1C; 0x09 = 0x0F;
    0x1B = 0x01; 0x26 = 0x48; 0x28 = 0x50; 0x25 = 0x4B; 0x27 = 0x4D; 0x57 = 0x11;
    0x53 = 0x1F; 0x41 = 0x1E; 0x44 = 0x20; 0x46 = 0x21; 0x31 = 0x02; 0x33 = 0x04;
    0x51 = 0x10; 0x45 = 0x12; 0x10 = 0x2A
}

function Get-GameWindow {
    $proc = Get-Process rb_blitz -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { throw "rb_blitz is not running" }
    # Windows may identify an IME/input-indicator overlay as MainWindowHandle.
    $h = [RbDriveWin32]::GameWindow($proc.Id)
    if ($h -eq [IntPtr]::Zero) { throw "rb_blitz has no main window" }
    return $h
}

function Focus-GameWindow {
    $h = Get-GameWindow
    if ([RbDriveWin32]::IsIconic($h)) { [RbDriveWin32]::ShowWindow($h, 9) | Out-Null }
    for ($i = 0; $i -lt 25; $i++) {
        if ([RbDriveWin32]::GetForegroundWindow() -eq $h) {
            Start-Sleep -Milliseconds 150
            return $h
        }
        # Tapping Alt is the documented way to hand this process the foreground
        # rights Windows withholds from a background process.
        [RbDriveWin32]::keybd_event(0x12, 0x38, 0, [IntPtr]::Zero)
        [RbDriveWin32]::keybd_event(0x12, 0x38, 2, [IntPtr]::Zero)
        [RbDriveWin32]::BringWindowToTop($h) | Out-Null
        [RbDriveWin32]::SetForegroundWindow($h) | Out-Null
        Start-Sleep -Milliseconds 150
    }
    throw "could not foreground the rb_blitz window; keys would reach the wrong window"
}

function Send-Key([int]$vk, [double]$seconds) {
    $scan = $scMap[$vk]
    if (-not $scan) { $scan = 0 }
    [RbDriveWin32]::keybd_event([byte]$vk, [byte]$scan, 0, [IntPtr]::Zero)
    if ($seconds -le 0) {
        Start-Sleep -Milliseconds 90
    } else {
        Start-Sleep -Milliseconds ([int]($seconds * 1000))
    }
    [RbDriveWin32]::keybd_event([byte]$vk, [byte]$scan, 2, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
}

function Resolve-Vk([string]$name) {
    $key = $name.ToLower()
    if (-not $vkMap.ContainsKey($key)) { throw "unknown key name: $name" }
    return [int]$vkMap[$key]
}

function Reset-GameFocus {
    $h = Get-GameWindow
    [RbDriveWin32]::ShowWindow($h, 6) | Out-Null   # SW_MINIMIZE -> SDL focus lost
    Start-Sleep -Milliseconds 500
    Focus-GameWindow | Out-Null
    Write-Host "refocused (minimize/restore)"
}

foreach ($action in $Actions) {
    # hold carries a third field (the seconds), so keep three parts.
    $parts = $action.Split(":", 3)
    switch ($parts[0]) {
        "wait" {
            Start-Sleep -Seconds ([double]$parts[1])
        }
        "refocus" {
            Reset-GameFocus
        }
        "shot" {
            $file = Join-Path $capDir $parts[1]
            & (Join-Path $PSScriptRoot "capture_window.ps1") -OutFile $file | Out-Null
            Write-Host "shot -> $file"
        }
        "key" {
            Focus-GameWindow | Out-Null
            Send-Key (Resolve-Vk $parts[1]) 0
            Write-Host "key $($parts[1]) sent"
        }
        "hold" {
            Focus-GameWindow | Out-Null
            Send-Key (Resolve-Vk $parts[1]) ([double]$parts[2])
            Write-Host "held $($parts[1]) for $($parts[2])s"
        }
        "mouse" {
            $h = Focus-GameWindow
            $rect = New-Object RbDriveWin32+Rect
            [RbDriveWin32]::GetClientRect($h, [ref]$rect) | Out-Null
            $point = New-Object RbDriveWin32+Point
            $point.x = [int](([double]$parts[1]) * $rect.right)
            $point.y = [int](([double]$parts[2]) * $rect.bottom)
            [RbDriveWin32]::ClientToScreen($h, [ref]$point) | Out-Null
            [RbDriveWin32]::SetCursorPos($point.x, $point.y) | Out-Null
        }
        "click" {
            Focus-GameWindow | Out-Null
            if ($parts[1] -eq "left") { $down = 2; $up = 4 }
            elseif ($parts[1] -eq "right") { $down = 8; $up = 16 }
            else { throw "unknown mouse button: $($parts[1])" }
            [RbDriveWin32]::mouse_event($down, 0, 0, 0, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 50
            [RbDriveWin32]::mouse_event($up, 0, 0, 0, [IntPtr]::Zero)
        }
        "wheel" {
            Focus-GameWindow | Out-Null
            [RbDriveWin32]::mouse_event(0x800, 0, 0, ([int]$parts[1] * 120), [IntPtr]::Zero)
        }
        "cursor" {
            $info = New-Object RbDriveWin32+CursorInfo
            $info.size = [Runtime.InteropServices.Marshal]::SizeOf($info)
            if (-not [RbDriveWin32]::GetCursorInfo([ref]$info)) { throw "GetCursorInfo failed" }
            $visible = ($info.flags -band 1) -ne 0
            if ($visible -ne ($parts[1] -eq "visible")) { throw "cursor visibility mismatch: $visible" }
            Write-Host "cursor $($parts[1]) verified"
        }
        default { throw "unknown action: $action" }
    }
}
Write-Host "done"
