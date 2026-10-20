# P2 of docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md: the data-layer probe.
#
# One boot, driven to the RhythmVerse Customs entry and into the retail store, with two
# instruments on:
#
#   * `--store_trace=1` — every game-root path the guest resolves or opens that names the store
#     (src/store/store_probe.cpp). This is what turns "the store asks for something" into a list
#     of names: the offer/metadata templates are the guest's own strings otherwise.
#   * `--store_local_server_root=<root>` — the boot rewrites `config/gen/store.dtb`'s
#     `netcache_init` so the store's default server is a local (host-file) one rooted there
#     (src/store/store_payload.h). With it, the names the store asks for become *file opens*
#     under the game root, which is the whole of Option A's path-mapping question; without it,
#     the same boot is the control leg that proves the retail server still gets selected.
#
# What it writes, under <OutDir>/<stamp>/: menu.png/txt, store.png/txt, store-back.png/txt, the
# run's log copy, and report.txt with the store-probe census and the screens' OCR text. It prints
# the report to stdout as well, so a terminal run is self-contained.
#
# The row index is a parameter on purpose: the main menu's rows depend on three settings
# (`interface.hide_download_content`, `cache_dlc_library`, and whether a DLC song is playing), so
# the script does not guess. Run it first with -MenuOnly, read menu.txt, and pass -StoreRow.
#
# Usage (execution policy is Restricted on this machine):
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_store.ps1 -MenuOnly
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_store.ps1 -StoreRow 5
#   # control leg: retail server, no local rewrite, trace still on
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_store.ps1 -StoreRow 5 -LocalRoot ""
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/_swap/store-probe",
    [string]$GameRoot,
    [string]$UltimateMode = "1",
    # The shipped `local` server's own root (out/_swap/config_store.tree.txt). Empty disables the
    # rewrite and makes the run the control leg.
    [string]$LocalRoot = "ui/store/remote_metadata/",
    [int]$StoreRow = 0,
    [switch]$MenuOnly,
    # One more accept inside the store, on its first menu row: that is where the panel requests
    # its metadata (`dlc_top_<a>_<b>.dta`), so the local-server question is only answered with it.
    [switch]$EnterStore,
    [string]$LogLevel = "debug",
    [int]$BootTimeoutSec = 180,
    [int]$ScreenTimeoutSec = 45,
    [int]$SettleAfterPressSec = 10,
    [int]$CloseWaitSec = 25
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work "rb_blitz.exe"
$logs = Join-Path $work "logs"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$capDir = Join-Path $root (Join-Path $OutDir $stamp)
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }

# ---------------------------------------------------------------- helpers ---

# Every helper that touches a .ps1 file goes through a child PowerShell: this machine's execution
# policy is Restricted (docs/guides/build-and-run.md §0).
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

function Capture-To([string]$Path) {
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

function Get-ScreenText([string]$Shot) {
    return ((Invoke-ChildScript "ocr_image.ps1" @{ Path = $Shot }) -join " ").ToUpperInvariant()
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

function Wait-ForNeedle([string]$Shot, [string]$Needle, [int]$TimeoutSec, [string]$Label) {
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
    Write-Host "  !! timed out after ${TimeoutSec}s waiting for '$Needle' ($Label); last text: $last"
    return $last
}

function Invoke-Actions([string[]]$Actions) {
    Invoke-ChildScript "drive_ui.ps1" @{ Actions = ($Actions -join ","); BuildDir = $BuildDir; OutDir = $OutDir } | Out-Null
}

$clampUp = @()
for ($i = 0; $i -lt 8; $i++) { $clampUp += "key:lstick_up" }

# ------------------------------------------------------------------ run ---

$before = Get-NewestLog
$arguments = @(
    "--game_data_root=$GameRoot",
    "--ultimate_mode=$UltimateMode",
    "--mnk_mode=1",
    "--no-mouse_ui_nav",
    "--log_level=$LogLevel",
    "--log_flush_interval=1",
    "--log_max_file_size_mb=200",
    "--store_trace=1"
)
if ($LocalRoot -ne "") { $arguments += "--store_local_server_root=$LocalRoot" }

Write-Host "probe_store: launching $exe"
Write-Host "  args: $($arguments -join ' ')"
$proc = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $arguments

$verdict = [ordered]@{
    stamp        = $stamp
    local_root   = $LocalRoot
    store_row    = $StoreRow
    menu_only    = [bool]$MenuOnly
    window       = $false
    title_text   = ""
    menu_text    = ""
    row_text     = ""
    store_text   = ""
    store_in_text = ""
    back_text    = ""
    dropped      = $false
}

try {
    $verdict.window = Wait-ForWindow $proc $BootTimeoutSec
    if (-not $verdict.window) { throw "no game window after ${BootTimeoutSec}s" }

    # The payload is installed ~50 s into the run and the title draws after that, so the route
    # starts from a measured title rather than a sleep: an accept sent before it lands is simply
    # lost (measured 2026-10-20: a fixed `wait:26` captured the title screen afterwards).
    $verdict.title_text = Wait-ForNeedle (Join-Path $capDir "title.png") "TO START" $BootTimeoutSec "title"
    # Title -> main menu. The build's own R3 skip is on by default, so one accept is the route
    # (scripts/ui_states.ps1's measured route with the skip on).
    Invoke-Actions @("key:a", "wait:8", "wait:6")
    $verdict.menu_text = Wait-ForNeedle (Join-Path $capDir "menu.png") "PLAY" $ScreenTimeoutSec "main menu"

    if ($MenuOnly) {
        "probe_store: -MenuOnly, stopping after the main menu" | Write-Host
        $verdict.dropped = $true
    } else {
        $route = @($clampUp)
        for ($d = 0; $d -lt $StoreRow; $d++) { $route += "key:lstick_down" }
        Invoke-Actions $route
        # The highlight is part of the evidence: a row can be drawn and still not be the one the
        # list would dispatch (measured 2026-10-20 while placing P1's entry), so the shot taken with
        # the row selected is kept beside the one the list opened.
        Capture-To (Join-Path $capDir "row.png")
        $verdict.row_text = Get-ScreenText (Join-Path $capDir "row.png")
        Invoke-Actions @("key:a")
        Start-Sleep -Seconds $SettleAfterPressSec
        Capture-To (Join-Path $capDir "store.png")
        $verdict.store_text = Get-ScreenText (Join-Path $capDir "store.png")
        if ($EnterStore) {
            Invoke-Actions @("key:a", "wait:3")
            Start-Sleep -Seconds $SettleAfterPressSec
            Capture-To (Join-Path $capDir "store-in.png")
            $verdict.store_in_text = Get-ScreenText (Join-Path $capDir "store-in.png")
        }
        # B returns to the menu (P1's acceptance asks for exactly this).
        Invoke-Actions @("key:b", "wait:4")
        Capture-To (Join-Path $capDir "store-back.png")
        $verdict.back_text = Get-ScreenText (Join-Path $capDir "store-back.png")
    }
} finally {
    $verdict.dropped = $verdict.dropped -or (-not $proc.HasExited)
    try {
        if (-not $proc.HasExited) {
            $proc.CloseMainWindow() | Out-Null
            if (-not $proc.WaitForExit($CloseWaitSec * 1000)) { $proc.Kill() }
        }
    } catch { }
}

# ---------------------------------------------------------------- report ---

Start-Sleep -Seconds 2
$newLog = Get-NewestLog
$logText = Get-LogText $newLog
if ($newLog -and $newLog -ne $before) {
    Copy-Item -LiteralPath $newLog -Destination (Join-Path $capDir "run.log") -Force
}

$probeLines = @()
$settingsLines = @()
$debugLines = @()
if ($logText) {
    $probeLines = ($logText -split "`r?`n") | Where-Object { $_ -match "store-probe:" }
    $settingsLines = ($logText -split "`r?`n") |
        Where-Object { $_ -match "settings_menu: (the P2|the RhythmVerse|the restored|mounted)" }
    $debugLines = ($logText -split "`r?`n") | Where-Object { $_ -match "OutputDebugString|RtlDebugTrace" }
}

$report = @()
$report += "probe_store $stamp"
$report += "build        : $BuildDir"
$report += "local root   : '$LocalRoot'$(if ($LocalRoot -eq '') { ' (control leg: retail server)' })"
$report += "store row    : $StoreRow$(if ($MenuOnly) { ' (menu only)' })"
$report += ""
$report += "--- main menu (OCR) ---"
$report += $verdict.menu_text
if (-not $MenuOnly) {
    $report += ""
    $report += "--- selected row (OCR) ---"
    $report += $verdict.row_text
    $report += ""
    $report += "--- store (OCR) ---"
    $report += $verdict.store_text
    if ($EnterStore) {
        $report += ""
        $report += "--- store, first menu row entered (OCR) ---"
        $report += $verdict.store_in_text
    }
    $report += ""
    $report += "--- after B (OCR) ---"
    $report += $verdict.back_text
}
$report += ""
$report += "--- store-probe lines ($($probeLines.Count)) ---"
$report += $probeLines
$report += ""
$report += "--- settings_menu lines ($($settingsLines.Count)) ---"
$report += $settingsLines
if ($debugLines.Count -gt 0) {
    $report += ""
    $report += "--- guest debug strings ($($debugLines.Count)) ---"
    $report += $debugLines
}

$reportPath = Join-Path $capDir "report.txt"
$report | Set-Content -LiteralPath $reportPath -Encoding utf8
$report | Write-Host

Write-Host ""
Write-Host "probe_store: wrote $reportPath"
