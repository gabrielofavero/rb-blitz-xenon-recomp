# Scan a running rb_blitz process for a string and report the guest address it
# lives at, plus the neighbouring strings.
#
# Why this exists: the retail XEX is encrypted and compressed, so the guest
# image cannot be searched on disk. At runtime the whole image is in the
# process, and the SDK maps it contiguously - guest address 0x82000000 is
# process address 0x0000000182000000 (arena virtual base 0x100000000, printed
# at startup as "Guest memory arena mapped"). Finding the address of a UI
# string lets us grep the generated C++ for the instruction that loads it and
# read the surrounding logic.
#
# Usage:
#   .\scripts\scan_process_strings.ps1 -Pattern "Rock Central"
#   .\scripts\scan_process_strings.ps1 -Pattern "Rock Central" -ContextChars 120
param(
    [Parameter(Mandatory = $true)][string]$Pattern,
    [int]$ContextChars = 80,
    [string]$ProcessName = "rb_blitz"
)

$ErrorActionPreference = "Stop"

if (-not ("RbScanWin32" -as [type])) {
Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class RbScanWin32 {
    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORY_BASIC_INFORMATION {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public IntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
    }
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] public static extern IntPtr VirtualQueryEx(IntPtr h, IntPtr addr, out MEMORY_BASIC_INFORMATION mbi, IntPtr size);
    [DllImport("kernel32.dll")] public static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buffer, IntPtr size, out IntPtr read);

    // Returns every byte offset at which 'needle' occurs, scanning readable,
    // committed regions only.
    public static List<ulong> Scan(int pid, byte[] needle) {
        List<ulong> hits = new List<ulong>();
        IntPtr h = OpenProcess(0x0410 /*QUERY_INFORMATION|VM_READ*/, false, pid);
        if (h == IntPtr.Zero) { throw new Exception("OpenProcess failed"); }
        try {
            IntPtr addr = IntPtr.Zero;
            MEMORY_BASIC_INFORMATION mbi;
            int mbiSize = Marshal.SizeOf(typeof(MEMORY_BASIC_INFORMATION));
            while (VirtualQueryEx(h, addr, out mbi, (IntPtr)mbiSize) != IntPtr.Zero) {
                long regionSize = mbi.RegionSize.ToInt64();
                bool readable = mbi.State == 0x1000 /*MEM_COMMIT*/ && (mbi.Protect & 0x100 /*GUARD*/) == 0 &&
                                (mbi.Protect & 0x02 /*READONLY*/) != 0 || (mbi.Protect & 0x04 /*READWRITE*/) != 0 ||
                                (mbi.Protect & 0x20 /*EXECUTE_READ*/) != 0 || (mbi.Protect & 0x40 /*EXECUTE_READWRITE*/) != 0 ||
                                (mbi.Protect & 0x08 /*WRITECOPY*/) != 0 || (mbi.Protect & 0x80 /*EXECUTE_WRITECOPY*/) != 0;
                if (readable && regionSize > 0 && regionSize < 512L * 1024 * 1024) {
                    byte[] buf = new byte[regionSize];
                    IntPtr read;
                    if (ReadProcessMemory(h, mbi.BaseAddress, buf, (IntPtr)regionSize, out read)) {
                        int n = (int)read.ToInt64();
                        for (int i = 0; i + needle.Length <= n; i++) {
                            bool ok = true;
                            for (int j = 0; j < needle.Length; j++) {
                                if (buf[i + j] != needle[j]) { ok = false; break; }
                            }
                            if (ok) { hits.Add((ulong)(mbi.BaseAddress.ToInt64() + i)); }
                        }
                    }
                }
                long next = mbi.BaseAddress.ToInt64() + regionSize;
                if (next <= addr.ToInt64()) { break; }
                addr = (IntPtr)next;
            }
        } finally { CloseHandle(h); }
        return hits;
    }

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

$arenaBase = 0x100000000
$variants = @{
    "ascii"   = [System.Text.Encoding]::ASCII.GetBytes($Pattern)
    "utf16le" = [System.Text.Encoding]::Unicode.GetBytes($Pattern)
}

foreach ($name in $variants.Keys) {
    $needle = $variants[$name]
    $hits = [RbScanWin32]::Scan($proc.Id, $needle)
    Write-Host "=== $name : $($hits.Count) hit(s) ==="
    foreach ($hit in $hits) {
        $guest = [int64]$hit - $arenaBase
        $guestText = if ($guest -ge 0 -and $guest -lt 0x100000000) { "guest=0x{0:X8}" -f $guest } else { "guest=n/a" }
        Write-Host ("  proc=0x{0:X} {1}" -f $hit, $guestText)
        $start = [int64]$hit - $ContextChars
        $bytes = [RbScanWin32]::Read($proc.Id, [uint64]$start, ($ContextChars * 2 + $needle.Length))
        if ($bytes) {
            $text = [System.Text.Encoding]::ASCII.GetString($bytes) -replace '[^\x20-\x7E]', '.'
            Write-Host "    ctx: $text"
        }
    }
}
