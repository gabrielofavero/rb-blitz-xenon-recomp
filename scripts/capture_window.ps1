# Capture a screenshot of the rb_blitz game window (or the full virtual screen
# as a fallback) to a PNG for inspection during bring-up.
param(
    [string]$OutFile = "out/bringup-capture.png"
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("shcore.dll")] public static extern int SetProcessDpiAwareness(int value);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

# Without this the window rect and the screen copy come back in DPI-virtualized
# units (the game window is a real 1920x1080 surface, but a scaled desktop makes
# it 1097x617), so UI text is captured too small to OCR. Must be set before the
# window is measured; the process is per-invocation, so nothing else is affected.
try { [Win32]::SetProcessDpiAwareness(2) | Out-Null } catch { [Win32]::SetProcessDPIAware() | Out-Null }

$proc = Get-Process rb_blitz -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Error "rb_blitz not running"; exit 1 }

$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Error "no main window"; exit 1 }

[Win32]::ShowWindow($h, 9) | Out-Null      # SW_RESTORE
# Bring the game window to the foreground and wait until it actually is.
$fg = $false
for ($i = 0; $i -lt 20; $i++) {
    [Win32]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 150
    if ([Win32]::GetForegroundWindow() -eq $h) { $fg = $true; break }
}
if (-not $fg) { Write-Warning "game window did not become foreground; capturing anyway" }
Start-Sleep -Milliseconds 400

$r = New-Object Win32+RECT
[Win32]::GetWindowRect($h, [ref]$r) | Out-Null
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
