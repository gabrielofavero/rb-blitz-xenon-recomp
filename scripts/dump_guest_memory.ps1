# Dump guest physical memory from a running rb_blitz process.
#
# Why this exists: the guest image is only in the process at runtime (the retail
# XEX is encrypted/compressed), so anything the game writes at runtime - e.g. the
# contents of a texture the GPU samples - can only be inspected live. The SDK maps
# the guest arena contiguously and prints the mapping at startup:
#   "Guest memory arena mapped: virtual base 0x..., physical base 0x..."
# so the host address of a guest *physical* address is physBase + (addr & 0x1FFFFFFF).
#
# Guest memory holds big-endian data; the GPU-order (byte-swapped) dword is printed
# alongside so the value can be compared directly against shader expectations.
#
# Usage:
#   .\scripts\dump_guest_memory.ps1 -Physical 0x0F7DA000 -Dwords 32
#   .\scripts\dump_guest_memory.ps1 -Physical 0x0F7DA000 -Offset 0x4000 -Dwords 16 -Samples 3 -IntervalMs 500
param(
    [Parameter(Mandatory = $true)][string]$Physical,
    [int]$Offset = 0,
    [int]$Dwords = 32,
    [int]$Samples = 1,
    [int]$IntervalMs = 500,
    [string]$ProcessName = "rb_blitz",
    [string]$LogDir = "out\build\win-amd64-release\logs"
)

$ErrorActionPreference = "Stop"

if (-not ("RbGuestMemWin32" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class RbGuestMemWin32 {
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buffer, IntPtr size, out IntPtr read);

    public static byte[] Read(int pid, ulong address, int length) {
        IntPtr h = OpenProcess(0x0410, false, pid);
        if (h == IntPtr.Zero) { throw new Exception("OpenProcess failed"); }
        try {
            byte[] buf = new byte[length];
            IntPtr read;
            if (!ReadProcessMemory(h, (IntPtr)(long)address, buf, (IntPtr)length, out read)) { return null; }
            int n = (int)read.ToInt64();
            if (n != length) { Array.Resize(ref buf, n); }
            return buf;
        } finally { CloseHandle(h); }
    }
}
"@
}

$proc = Get-Process $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { throw "$ProcessName is not running" }

$log = Get-ChildItem (Join-Path $LogDir "*.log") -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $log) { throw "no logs in $LogDir" }
$stream = [System.IO.File]::Open($log.FullName, 'Open', 'Read', 'ReadWrite')
$reader = New-Object System.IO.StreamReader($stream)
$text = $reader.ReadToEnd()
$reader.Close()
$m = [regex]::Match($text, 'physical base 0x([0-9A-Fa-f]+)')
if (-not $m.Success) { throw "could not find the physical membase in $($log.Name)" }
$physBase = [uint64]("0x" + $m.Groups[1].Value)
Write-Host ("physical membase 0x{0:X16} (from {1}), pid {2}" -f $physBase, $log.Name, $proc.Id)

$guest = ([uint64]("0x" + $Physical.Replace("0x", "")) + [uint64]$Offset)
$hostAddr = $physBase + ($guest -band 0x1FFFFFFF)

for ($s = 0; $s -lt $Samples; $s++) {
    $bytes = [RbGuestMemWin32]::Read($proc.Id, $hostAddr, ($Dwords * 4))
    if (-not $bytes) { throw ("ReadProcessMemory failed at host 0x{0:X}" -f $hostAddr) }
    Write-Host ("--- sample {0}: guest=0x{1:X8} host=0x{2:X} ({3} bytes) ---" -f $s, $guest, $hostAddr, $bytes.Length)
    for ($i = 0; $i -lt [Math]::Floor($bytes.Length / 4); $i++) {
        $be = [System.BitConverter]::ToUInt32(($bytes[4 * $i + 3], $bytes[4 * $i + 2], $bytes[4 * $i + 1], $bytes[4 * $i]), 0)
        $gpu = [System.BitConverter]::ToUInt32($bytes, 4 * $i)
        $gpuF = [System.BitConverter]::ToSingle($bytes, 4 * $i)
        $beF = [System.BitConverter]::ToSingle(($bytes[4 * $i + 3], $bytes[4 * $i + 2], $bytes[4 * $i + 1], $bytes[4 * $i]), 0)
        Write-Host ("  +{0:X4}  bigendian=0x{1:X8}  gpu=0x{2:X8}  gpu_f32={3}  bigendian_f32={4}" -f (4 * $i), $be, $gpu, $gpuF, $beF)
    }
    if ($s -lt ($Samples - 1)) { Start-Sleep -Milliseconds $IntervalMs }
}
