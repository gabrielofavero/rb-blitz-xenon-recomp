# Capture a screenshot of the rb_blitz game window (or the full virtual screen
# as a fallback) to a PNG for inspection during bring-up.
param(
    [string]$OutFile = "out/bringup-capture.png",
    # Which window to capture. The game is the default; the launcher's A1 run
    # captures rb_blitz_launcher the same way.
    [string]$ProcessName = "rb_blitz",
    # Capture the client area - the pixels the process itself drew - instead of the
    # window rect, which also holds the frame and the title bar. The offset the
    # chrome adds is the display's content scale times the frame's own metrics, so a
    # harness that crops a region out of the picture (scripts/capture_launcher.ps1)
    # cannot guess it: with this switch the picture starts where the UI starts. The
    # game's captures are of a fullscreen window and take the default.
    [switch]$ClientArea
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int value);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

# Without this the window rect and the screen copy come back in DPI-virtualized
# units (the game window is a real 1920x1080 surface, but a scaled desktop makes
# it 1097x617), so UI text is captured too small to OCR. Must be set before the
# window is measured; the process is per-invocation, so nothing else is affected.
try { [Win32]::SetProcessDpiAwareness(2) | Out-Null } catch { [Win32]::SetProcessDPIAware() | Out-Null }

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Error "$ProcessName not running"; exit 1 }

$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Error "no main window"; exit 1 }

[Win32]::ShowWindow($h, 9) | Out-Null      # SW_RESTORE
# Bring the game window to the foreground and wait until it actually is.
# Windows withholds the foreground from a background process, so tapping Alt
# (which grants the caller the right to hand it out) is what makes this
# converge when another window is in front - the same trick scripts/drive_ui.ps1
# uses before sending keys. The copy below is a screen capture, so waiting on
# the real foreground is not optional: capturing an occluding window would
# silently save some other application's UI.
$fg = $false
for ($i = 0; $i -lt 25; $i++) {
    if ([Win32]::GetForegroundWindow() -eq $h) { $fg = $true; break }
    [Win32]::keybd_event(0x12, 0x38, 0, [IntPtr]::Zero)   # Alt down
    [Win32]::keybd_event(0x12, 0x38, 2, [IntPtr]::Zero)   # Alt up
    [Win32]::BringWindowToTop($h) | Out-Null
    [Win32]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 150
}
if (-not $fg) { Write-Error "game window did not come to the foreground; refusing to save a capture of the wrong window"; exit 1 }
Start-Sleep -Milliseconds 400

$r = New-Object Win32+RECT
[Win32]::GetWindowRect($h, [ref]$r) | Out-Null
if ($ClientArea) {
    # GetClientRect's left/top are always 0: it answers in the client area's own
    # coordinates. Where that rectangle is on the screen is ClientToScreen's answer for
    # the client origin, so the two together are the client rectangle in screen space,
    # which is what CopyFromScreen below wants.
    $client = New-Object Win32+RECT
    if (-not [Win32]::GetClientRect($h, [ref]$client)) { Write-Error "no client rect"; exit 1 }
    $origin = New-Object Win32+POINT
    if (-not [Win32]::ClientToScreen($h, [ref]$origin)) { Write-Error "no client origin"; exit 1 }
    $r.Left = $origin.X
    $r.Top = $origin.Y
    $r.Right = $origin.X + $client.Right
    $r.Bottom = $origin.Y + $client.Bottom
}
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
if ($w -le 0 -or $ht -le 0) { Write-Error "bad window rect"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
$dir = Split-Path -Parent $OutFile
if ($dir -and -not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
$outPath = [System.IO.Path]::GetFullPath($OutFile)
$bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "saved $outPath ($w x $ht)"
