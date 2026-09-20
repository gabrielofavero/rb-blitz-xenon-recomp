# Sample the instruction pointer and a shallow stack trace of a thread in a
# running rb_blitz process, for diagnosing guest hangs and spin loops during
# bring-up (no debugger required).
#
# Each sample suspends the target thread just long enough to read RIP plus the
# top of the stack, then resumes it. Addresses are resolved to the owning module
# (or reported as "unmapped" - which is where the recompiled guest code lives),
# so the output shows whether a spin is in the runtime, the GPU plugin, or the
# recompiled guest itself.
#
# Usage:
#   .\scripts\sample_thread.ps1                      # busiest thread, 5 samples
#   .\scripts\sample_thread.ps1 -ThreadId 8632 -Count 3
#   .\scripts\sample_thread.ps1 -List                # list threads with CPU time
param(
    [int]$ProcessId = 0,
    [int]$ThreadId = 0,
    [int]$Count = 5,
    [int]$StackWords = 24,
    [int]$IntervalMs = 250,
    [switch]$List
)

$ErrorActionPreference = "Stop"

if (-not ("RbProbe" -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class RbProbe {
    [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr OpenThread(uint access, bool inherit, int tid);
    [DllImport("kernel32.dll")] public static extern int SuspendThread(IntPtr h);
    [DllImport("kernel32.dll")] public static extern int ResumeThread(IntPtr h);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool GetThreadContext(IntPtr h, IntPtr ctx);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool ReadProcessMemory(IntPtr p, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);

    public const uint PROCESS_VM_READ = 0x0010;
    public const uint PROCESS_QUERY_INFORMATION = 0x0400;
    public const uint THREAD_SUSPEND_RESUME = 0x0002;
    public const uint THREAD_GET_CONTEXT = 0x0008;
    public const uint CONTEXT_CONTROL_INTEGER = 0x00100003;

    // Offsets within CONTEXT for AMD64 (see winnt.h).
    public const int OffRsp = 0x98;
    public const int OffRip = 0xF8;

    public static ulong[] SampleSpin(int pid, int tid, int stackWords, out ulong rip, out ulong rsp) {
        rip = 0; rsp = 0;
        IntPtr proc = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, false, pid);
        IntPtr thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, false, tid);
        if (proc == IntPtr.Zero || thread == IntPtr.Zero) { throw new Exception("OpenProcess/OpenThread failed"); }
        try {
            // CONTEXT must be 16-byte aligned.
            IntPtr raw = Marshal.AllocHGlobal(1280 + 16);
            long aligned = ((long)raw + 15) & ~15L;
            IntPtr ctx = (IntPtr)aligned;
            try {
                byte[] zero = new byte[1280];
                Marshal.Copy(zero, 0, ctx, 1280);
                Marshal.WriteInt32(ctx, 0x30, unchecked((int)CONTEXT_CONTROL_INTEGER));
                if (SuspendThread(thread) == -1) { throw new Exception("SuspendThread failed"); }
                try {
                    if (!GetThreadContext(thread, ctx)) { throw new Exception("GetThreadContext failed"); }
                    rip = (ulong)Marshal.ReadInt64(ctx, OffRip);
                    rsp = (ulong)Marshal.ReadInt64(ctx, OffRsp);
                } finally { ResumeThread(thread); }

                ulong[] words = new ulong[stackWords];
                IntPtr read = IntPtr.Zero;
                byte[] buf = new byte[stackWords * 8];
                if (ReadProcessMemory(proc, (IntPtr)(long)rsp, buf, (IntPtr)buf.Length, out read) && (long)read == buf.Length) {
                    for (int i = 0; i < stackWords; i++) { words[i] = BitConverter.ToUInt64(buf, i * 8); }
                }
                return words;
            } finally { Marshal.FreeHGlobal(raw); }
        } finally { CloseHandle(thread); CloseHandle(proc); }
    }
}
"@
}

$proc = if ($ProcessId -ne 0) { Get-Process -Id $ProcessId } else { Get-Process rb_blitz | Select-Object -First 1 }
if (-not $proc) { Write-Error "process not found"; exit 1 }

$modules = @()
foreach ($m in $proc.Modules) { $modules += [pscustomobject]@{ Base = $m.BaseAddress.ToInt64(); End = $m.BaseAddress.ToInt64() + $m.ModuleMemorySize; Name = $m.ModuleName } }
function Resolve-Addr([uint64]$a) {
    foreach ($m in $modules) { if ($a -ge $m.Base -and $a -lt $m.End) { return ("{0}+0x{1:x}" -f $m.Name, ($a - $m.Base)) } }
    return "UNMAPPED"
}

# The runtime's own thread names (from the log) help label the CPU users.
$known = @{ 10340 = 'XMA Decoder'; 24164 = 'Audio Worker'; 14244 = 'GPU Commands'; 20356 = 'GPU VSync'; 20072 = 'Kernel Dispatch'; 8632 = 'guest main'; 12104 = 'SDL input' }

$targets = @()
if ($List) {
    $targets = $proc.Threads | Sort-Object { $_.TotalProcessorTime.TotalSeconds } -Descending | Select-Object -First 10
    foreach ($t in $targets) {
        $name = if ($known.ContainsKey([int]$t.Id)) { $known[[int]$t.Id] } else { '' }
        "{0,6}  {1,-16} {2,-16} cpu={3,8:N1}s" -f $t.Id, $name, $t.ThreadState, $t.TotalProcessorTime.TotalSeconds
    }
    exit 0
}

if ($ThreadId -ne 0) {
    $targets = @($proc.Threads | Where-Object { $_.Id -eq $ThreadId })
    if (-not $targets) { Write-Error "thread $ThreadId not found"; exit 1 }
} else {
    $targets = @($proc.Threads | Sort-Object { $_.TotalProcessorTime.TotalSeconds } -Descending | Select-Object -First 1)
}

foreach ($t in $targets) {
    $name = if ($known.ContainsKey([int]$t.Id)) { $known[[int]$t.Id] } else { '' }
    "=== thread {0} {1} ({2}) ===" -f $t.Id, $name, $t.ThreadState
    for ($i = 0; $i -lt $Count; $i++) {
        $rip = [uint64]0; $rsp = [uint64]0
        $words = [RbProbe]::SampleSpin($proc.Id, [int]$t.Id, $StackWords, [ref]$rip, [ref]$rsp)
        "sample {0}: rip={1:x16} ({2})  rsp={3:x16}" -f $i, $rip, (Resolve-Addr $rip), $rsp
        $idx = 0
        foreach ($w in $words) {
            if ($w -ge 0x10000 -and $w -lt 0x0000800000000000) {
                $r = Resolve-Addr $w
                if ($r -ne "UNMAPPED") { "            [rsp+{0,3:x}] {1:x16} {2}" -f ($idx * 8), $w, $r }
            }
            $idx++
        }
        Start-Sleep -Milliseconds 250
    }
}
