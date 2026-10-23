# P2 of docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md: the data-layer probe.
#
# One boot, driven to the RhythmVerse entry and into the retail store, with two
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
#   # a served page, then the A press on its first song (the details screen)
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_store.ps1 -StoreRow 3 -EnterStore -PageRow 0 -OfferRow 0
#   # control leg: retail server, no local rewrite, trace still on
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_store.ps1 -StoreRow 5 -LocalRoot ""
#   # second visit: leave the store, wait, walk back in and press the page row again. This is the
#   # route `rb_blitz_238.log` (2026-10-22, 20:09) crashed on, and the probe has `store_trace` on,
#   # so its fault logger prints the stack the shipping boot did not have.
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_store.ps1 -StoreRow 4 -PageRow 0 -Reenter -ReenterWaitSec 15
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
    # After entering the store, move this many rows down before accepting, and keep the screen that
    # opens as `page-in`. -1 leaves the route as it was. The store's own rows are retail's
    # (MESSAGES, RECOMMENDED, YOUR SONGS) followed by the served front menu's (RECENT, SEARCH), so
    # the Recent page is row 3 on a build whose front menu is the two owner entries.
    [int]$PageRow = -1,
    # The drive_ui name of the key that opens the row (`key:a` is the pad's A). Retail's store also
    # binds Start to Help & Options, so a boot that lands there instead of the page says the press
    # arrived as Start - which is a binding question, not a store one.
    [string]$PageKey = "key:a",
    # After the page opens, move this many rows down and accept once more: that is the A press on a
    # song, which is what opens the details screen (`store_info_screen`). -1 leaves the route as it
    # was. The page's rows are the served offers, so 0 is the newest one.
    [int]$OfferRow = -1,
    # After the details page opens (P5), move this many rows down and accept once more: that is the
    # press on a button of the details page. 0 is the first row, which is where this build's
    # Download row sits (retail inserts Exit first and every other row at the front). -1 leaves the
    # route as it was.
    [int]$InfoRow = -1,
    # How long to let a download run after that press before the last capture. A small custom is
    # seconds; the wait is what makes the strip's own progress and outcome readable.
    [int]$DownloadWaitSec = 40,
    # Second visit (P9 route, 2026-10-22): after the page steps, Back out to the main menu, wait,
    # walk back in through the store row and press the page row again. The owner's crash
    # (`rb_blitz_238.log`, `read of guest 0x00000004`) happened on exactly that second press, when
    # the Downloaded index had landed between the visits; the script always runs `store_trace`, so
    # the fault logger captures the stack the shipping boot did not have.
    [switch]$Reenter,
    [int]$ReenterWaitSec = 0,
    [string]$LogLevel = "debug",
    # Turns on the SDK's noisy kernel-import logging (the `log_noisy` cvar), which is what makes a
    # trace-level run name the exports the guest calls after the store is entered.
    [switch]$Noisy,
    # Extra command-line switches, ';'-separated (the other acceptance scripts' spelling). The
    # useful one here is a small DLC library: the boot waits for the content pipeline, so a
    # 1,400-package root costs minutes before the title is even reachable.
    [string]$Extra = "",
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
#
# The child's stderr is *collected as text*, not left as error records: `$ErrorActionPreference =
# "Stop"` turns a native command's stderr into a terminating error, and `capture_window.ps1` says
# things like "no window yet" on stderr during a scene change - which killed whole probe runs
# (measured 2026-10-22, three boots in a row). The exit code is still checked below.
function Invoke-ChildScript([string]$Script, [hashtable]$Named) {
    $argv = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) { $argv += @("-$k", [string]$Named[$k]) }
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & powershell @argv 2>&1 | ForEach-Object { "$_" }
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($LASTEXITCODE -ne 0) { throw "$Script failed: $($out -join ' ')" }
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
if ($Noisy) { $arguments += "--log_noisy=1" }
if ($Extra -ne "") { $arguments += ($Extra -split ";" | Where-Object { $_ -ne "" }) }

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
    page_text    = ""
    page_in_text = ""
    page_late_text = ""
    offer_text   = ""
    offer_late_text = ""
    info_text    = ""
    press_text   = ""
    press_done_text = ""
    reenter_text = ""
    reenter_page_text = ""
    reenter_page_late_text = ""
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
    # (scripts/ui_states.ps1's measured route with the skip on). The needle is the second row's own
    # label when OCR loses "PLAY" to the selection highlight, which is what a four-row menu (the
    # RhythmVerse entry on) does on some frames: both spellings only exist on this screen.
    Invoke-Actions @("key:a", "wait:8", "wait:6")
    try {
        $verdict.menu_text = Wait-ForNeedle (Join-Path $capDir "menu.png") "PLAY" $ScreenTimeoutSec "main menu"
    } catch {
        $verdict.menu_text = Wait-ForNeedle (Join-Path $capDir "menu.png") "EXIT GAME" $ScreenTimeoutSec "main menu (second spelling)"
    }

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
        if ($EnterStore -and $PageRow -lt 0) {
            Invoke-Actions @("key:a", "wait:3")
            Start-Sleep -Seconds $SettleAfterPressSec
            Capture-To (Join-Path $capDir "store-in.png")
            $verdict.store_in_text = Get-ScreenText (Join-Path $capDir "store-in.png")
        }
        if ($PageRow -ge 0) {
            # One tap per `wait:1`: a run of `lstick_down` without a pause between taps is dropped
            # by the guest (measured 2026-10-12), and the row index is the point of the parameter.
            $pageRoute = @()
            for ($d = 0; $d -lt $PageRow; $d++) { $pageRoute += @("key:lstick_down", "wait:1") }
            if ($pageRoute.Count -gt 0) { Invoke-Actions $pageRoute }
            Capture-To (Join-Path $capDir "page.png")
            $verdict.page_text = Get-ScreenText (Join-Path $capDir "page.png")
            Invoke-Actions @($PageKey)
            Start-Sleep -Seconds 2
            Capture-To (Join-Path $capDir "page-in.png")
            $verdict.page_in_text = Get-ScreenText (Join-Path $capDir "page-in.png")
            Start-Sleep -Seconds $SettleAfterPressSec
            Capture-To (Join-Path $capDir "page-late.png")
            $verdict.page_late_text = Get-ScreenText (Join-Path $capDir "page-late.png")
            if ($OfferRow -ge 0) {
                $offerRoute = @()
                for ($d = 0; $d -lt $OfferRow; $d++) { $offerRoute += @("key:lstick_down", "wait:1") }
                if ($offerRoute.Count -gt 0) { Invoke-Actions $offerRoute }
                Invoke-Actions @($PageKey)
                Start-Sleep -Seconds 2
                Capture-To (Join-Path $capDir "offer.png")
                $verdict.offer_text = Get-ScreenText (Join-Path $capDir "offer.png")
                Start-Sleep -Seconds $SettleAfterPressSec
                Capture-To (Join-Path $capDir "offer-late.png")
                $verdict.offer_late_text = Get-ScreenText (Join-Path $capDir "offer-late.png")
                if ($InfoRow -ge 0) {
                    # P5: the press that starts a download. Rows are moved one at a time here for
                    # the same reason the page's rows are, and the shot taken after the press is the
                    # strip's own first state (it draws over the panel).
                    $infoRoute = @()
                    for ($d = 0; $d -lt $InfoRow; $d++) { $infoRoute += @("key:lstick_down", "wait:1") }
                    if ($infoRoute.Count -gt 0) { Invoke-Actions $infoRoute }
                    Capture-To (Join-Path $capDir "info.png")
                    $verdict.info_text = Get-ScreenText (Join-Path $capDir "info.png")
                    Invoke-Actions @($PageKey)
                    Start-Sleep -Seconds 3
                    Capture-To (Join-Path $capDir "press.png")
                    $verdict.press_text = Get-ScreenText (Join-Path $capDir "press.png")
                    Start-Sleep -Seconds $DownloadWaitSec
                    Capture-To (Join-Path $capDir "press-done.png")
                    $verdict.press_done_text = Get-ScreenText (Join-Path $capDir "press-done.png")
                }
            }
        }
        if ($Reenter) {
            # Leave the store (one Back per screen the route opened), wait for the Downloaded index
            # to land, then walk the same route again. `-ReenterWaitSec` is the parameter because a
            # 1,700-package library needs its index built before the second page carries marks;
            # a two-package one is ready almost immediately.
            $backs = if ($PageRow -ge 0) { @("key:b", "wait:2", "key:b", "wait:4") } else { @("key:b", "wait:4") }
            Invoke-Actions $backs
            Capture-To (Join-Path $capDir "reenter-left.png")
            Start-Sleep -Seconds $ReenterWaitSec
            $again = @($clampUp)
            for ($d = 0; $d -lt $StoreRow; $d++) { $again += "key:lstick_down" }
            $again += @("key:a", "wait:8")
            Invoke-Actions $again
            Capture-To (Join-Path $capDir "reenter.png")
            $verdict.reenter_text = Get-ScreenText (Join-Path $capDir "reenter.png")
            if ($PageRow -ge 0) {
                $againRoute = @()
                for ($d = 0; $d -lt $PageRow; $d++) { $againRoute += @("key:lstick_down", "wait:1") }
                if ($againRoute.Count -gt 0) { Invoke-Actions $againRoute }
                Invoke-Actions @($PageKey)
                Start-Sleep -Seconds 2
                Capture-To (Join-Path $capDir "reenter-page.png")
                $verdict.reenter_page_text = Get-ScreenText (Join-Path $capDir "reenter-page.png")
                Start-Sleep -Seconds $SettleAfterPressSec
                Capture-To (Join-Path $capDir "reenter-page-late.png")
                $verdict.reenter_page_late_text = Get-ScreenText (Join-Path $capDir "reenter-page-late.png")
            }
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
$downloadLines = @()
$debugLines = @()
if ($logText) {
    $probeLines = ($logText -split "`r?`n") | Where-Object { $_ -match "store-probe:" }
    # P5's own lines: the pipeline's decisions (what was registered, what the transfer did, what was
    # installed) and the boot's refresh, which is what makes the package visible to the library.
    $downloadLines = ($logText -split "`r?`n") |
        Where-Object { $_ -match "store_download:|store_install:|store_catalogue: |dlc: the|dlc: R7|RHYTHMVERSE" }
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
    if ($EnterStore -and $PageRow -lt 0) {
        $report += ""
        $report += "--- store, first menu row entered (OCR) ---"
        $report += $verdict.store_in_text
    }
    if ($PageRow -ge 0) {
        $report += ""
        $report += "--- store, row $PageRow selected (OCR) ---"
        $report += $verdict.page_text
        $report += ""
        $report += "--- store, row $PageRow opened, +2 s (OCR) ---"
        $report += $verdict.page_in_text
        $report += ""
        $report += "--- store, row $PageRow opened, settle (OCR) ---"
        $report += $verdict.page_late_text
        if ($OfferRow -ge 0) {
            $report += ""
            $report += "--- offer row $OfferRow opened, +2 s (OCR) ---"
            $report += $verdict.offer_text
            $report += ""
            $report += "--- offer row $OfferRow opened, settle (OCR) ---"
            $report += $verdict.offer_late_text
            if ($InfoRow -ge 0) {
                $report += ""
                $report += "--- details row $InfoRow selected (OCR) ---"
                $report += $verdict.info_text
                $report += ""
                $report += "--- details row $InfoRow pressed, +3 s (OCR) ---"
                $report += $verdict.press_text
                $report += ""
                $report += "--- after ${DownloadWaitSec}s (OCR) ---"
                $report += $verdict.press_done_text
            }
        }
    }
    if ($Reenter) {
        $report += ""
        $report += "--- second visit: store (OCR) ---"
        $report += $verdict.reenter_text
        if ($PageRow -ge 0) {
            $report += ""
            $report += "--- second visit: row $PageRow opened, +2 s (OCR) ---"
            $report += $verdict.reenter_page_text
            $report += ""
            $report += "--- second visit: row $PageRow opened, settle (OCR) ---"
            $report += $verdict.reenter_page_late_text
        }
    }
    $report += ""
    $report += "--- after B (OCR) ---"
    $report += $verdict.back_text
}
$report += ""
$report += "--- store-probe lines ($($probeLines.Count)) ---"
$report += $probeLines
$report += ""
$report += "--- download / refresh lines ($($downloadLines.Count)) ---"
$report += $downloadLines
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
