# P9-10's failure measurement (docs/plans/finished/2026-10-13-rhythmverse-store-plan.md §9): a
# boot whose catalogue cannot be reached, then can, without touching the machine's network.
#
# The game is pointed at `scripts/diagnostics/store_stub_api.py` with `--store_api_base` (the diagnostics cvar):
#
#   1. the stub refuses every request -> the RhythmVerse row's press must draw the retail connection
#      popup and ask for a fresh walk (`a press asked`), and the row must stay on the menu;
#   2. a second press (the popup acknowledged, the row walked back into) must draw the same popup
#      and start another walk;
#   3. the script then flips the stub to serving a captured list page, and a third press must open
#      the store, whose Recent page lists the served offers.
#
# That is the prompt's three failure clauses in one boot, which is the point of the stub: the
# hosts file or a firewall rule can only produce clause 1 and 2.
#
# Usage (execution policy is Restricted on this machine):
#   python scripts/diagnostics/store_stub_api.py --port 8765 --payload <list page>.json --fail-flag <flag> ...
#   # the payload is one real API body, captured with the client's own form body:
#   #   Invoke-WebRequest https://rhythmverse.co/api/rb3xbox/songfiles/list -Method Post `
#   #     -Body "sort%5B0%5D%5Bsort_by%5D=update_date&sort%5B0%5D%5Bsort_order%5D=DESC&data_type=full&page=1&records=25"
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\diagnostics\probe_store_failure.ps1 `
#       -StoreRow 4 -PageRow 3 -Payload out/_swap/p910/list-page1.json
#
# Writes <OutDir>/<stamp>/ (menu/row/popup/popup2/serving/page PNGs and their OCR text, run.log, the
# stub's log, report.txt) and prints the verdicts. It never asserts the game's own wording; the
# checks are on the screens' text and on the host's log lines, which is what the record needs.
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/_swap/p910-fail",
    [string]$GameRoot,
    [string]$UltimateMode = "1",
    # The main menu row index that carries the RhythmVerse entry (a hint, as in probe_store.ps1: the
    # list's own rows depend on the boot's settings). The route clamps to the top first.
    [int]$StoreRow = 4,
    # The store front menu's Recent row, pressed after the third press opens the store. -1 skips it.
    [int]$PageRow = 3,
    [string]$PageKey = "key:a",
    [int]$Port = 8765,
    [string]$Payload = "",
    [string]$StubScript = "",
    [int]$BootTimeoutSec = 240,
    [int]$ScreenTimeoutSec = 90,
    [int]$SettleAfterPressSec = 8,
    [int]$CloseWaitSec = 25,
    [string]$LogLevel = "debug",
    [string]$Extra = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work = Join-Path $root $BuildDir
$exe = Join-Path $work "rb_blitz.exe"
$logs = Join-Path $work "logs"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
if (-not $StubScript) { $StubScript = Join-Path $PSScriptRoot "store_stub_api.py" }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$capDir = Join-Path $root (Join-Path $OutDir $stamp)
New-Item -ItemType Directory -Force -Path $capDir | Out-Null
if (-not $Payload) { $Payload = Join-Path $capDir "list-page1.json" }
$stubLog = Join-Path $capDir "stub.log"
$failFlag = Join-Path $capDir "serve-fail.flag"
$readyFlag = Join-Path $capDir "stub-ready.flag"

if (-not (Test-Path $exe)) { throw "not found: $exe" }
if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) { throw "no default.xex under $GameRoot" }
if (-not (Test-Path $Payload)) { throw "no payload at $Payload (see the header for the capture command)" }
if (Get-Process rb_blitz -ErrorAction SilentlyContinue) { throw "close the running game first" }
# The repository root-relative spelling: drive_ui joins its -OutDir against the repository root, so
# an absolute value would become `<root>\D:\...` (measured by the acceptance harness, 2026-10-22).
$outRel = Join-Path $OutDir $stamp

# ---------------------------------------------------------------- helpers ---

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
    Invoke-ChildScript "drive_ui.ps1" @{ Actions = ($Actions -join ","); BuildDir = $BuildDir; OutDir = $outRel } | Out-Null
}

# The route to the store row: clamp to the first row, then walk down. Every walk re-routes, because
# the list's selection survives a popup and a route that assumed it would land on another row.
function Get-RowRoute([int]$Row) {
    $route = @()
    for ($i = 0; $i -lt 8; $i++) { $route += "key:lstick_up" }
    for ($d = 0; $d -lt $Row; $d++) { $route += "key:lstick_down" }
    return $route
}

$verdict = [ordered]@{}
function Note([string]$Key, [string]$Value) {
    $verdict[$Key] = $Value
    Write-Host ("  {0,-22} {1}" -f $Key, ($Value -replace "`r?`n", " | "))
}

# ------------------------------------------------------------------ stub ---

Set-Content -LiteralPath $failFlag -Value "failing" -NoNewline
$stub = Start-Process -FilePath "python" -PassThru -NoNewWindow -ArgumentList @(
    $StubScript, "--port", "$Port", "--payload", $Payload, "--fail-flag", $failFlag,
    "--log", $stubLog, "--ready-flag", $readyFlag
)
$readyDeadline = (Get-Date).AddSeconds(20)
while (-not (Test-Path $readyFlag) -and (Get-Date) -lt $readyDeadline -and -not $stub.HasExited) {
    Start-Sleep -Milliseconds 200
}
if ($stub.HasExited -or -not (Test-Path $readyFlag)) { throw "the stub did not start (see $stubLog)" }
Write-Host "probe_store_failure: stub listening on http://127.0.0.1:$Port (failing)"

# ------------------------------------------------------------------- run ---

$before = Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
$arguments = @(
    "--game_data_root=$GameRoot",
    "--ultimate_mode=$UltimateMode",
    "--mnk_mode=1",
    "--no-mouse_ui_nav",
    "--log_level=$LogLevel",
    "--log_flush_interval=1",
    "--log_max_file_size_mb=200",
    "--store_api_base=http://127.0.0.1:$Port"
)
if ($Extra -ne "") { $arguments += ($Extra -split ";" | Where-Object { $_ -ne "" }) }

Write-Host "probe_store_failure: launching $exe"
Write-Host "  args: $($arguments -join ' ')"
$proc = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $arguments
$bootLog = ""

try {
    if (-not (Wait-ForWindow $proc $BootTimeoutSec)) { throw "no game window after ${BootTimeoutSec}s" }
    Note "title" (Wait-ForNeedle (Join-Path $capDir "title.png") "TO START" $BootTimeoutSec "title")
    Invoke-Actions @("key:a", "wait:8", "wait:6")
    try {
        Note "menu" (Wait-ForNeedle (Join-Path $capDir "menu.png") "PLAY" $ScreenTimeoutSec "main menu")
    } catch {
        Note "menu" (Wait-ForNeedle (Join-Path $capDir "menu.png") "EXIT GAME" $ScreenTimeoutSec "main menu (second spelling)")
    }

    # --- press 1: the catalogue holds nothing and its last walk failed -> popup + fresh walk ---- #
    Invoke-Actions (Get-RowRoute $StoreRow)
    Capture-To (Join-Path $capDir "row.png")
    Note "row" (Get-ScreenText (Join-Path $capDir "row.png"))
    Invoke-Actions @("key:a")
    Start-Sleep -Seconds $SettleAfterPressSec
    Capture-To (Join-Path $capDir "popup.png")
    Note "popup" (Get-ScreenText (Join-Path $capDir "popup.png"))

    # --- press 2: acknowledge, walk back in, press again -> the same popup, another walk -------- #
    Invoke-Actions @("key:a", "wait:3")
    Invoke-Actions (Get-RowRoute $StoreRow)
    Invoke-Actions @("key:a")
    Start-Sleep -Seconds $SettleAfterPressSec
    Capture-To (Join-Path $capDir "popup2.png")
    Note "popup2" (Get-ScreenText (Join-Path $capDir "popup2.png"))

    # --- press 3: the site answers -> the store opens on the served offers ---------------------- #
    Remove-Item -LiteralPath $failFlag -ErrorAction SilentlyContinue
    Write-Host "probe_store_failure: stub now serving $Payload"
    Invoke-Actions @("key:a", "wait:3")
    Invoke-Actions (Get-RowRoute $StoreRow)
    Invoke-Actions @("key:a")
    Start-Sleep -Seconds $SettleAfterPressSec
    Capture-To (Join-Path $capDir "serving.png")
    Note "serving" (Get-ScreenText (Join-Path $capDir "serving.png"))
    if ($PageRow -ge 0) {
        $pageRoute = @()
        for ($d = 0; $d -lt $PageRow; $d++) { $pageRoute += @("key:lstick_down", "wait:1") }
        if ($pageRoute.Count -gt 0) { Invoke-Actions $pageRoute }
        Invoke-Actions @($PageKey)
        Start-Sleep -Seconds 2
        Capture-To (Join-Path $capDir "page.png")
        Note "page" (Get-ScreenText (Join-Path $capDir "page.png"))
        Start-Sleep -Seconds $SettleAfterPressSec
        Capture-To (Join-Path $capDir "page-late.png")
        Note "page_late" (Get-ScreenText (Join-Path $capDir "page-late.png"))
    }
} finally {
    try {
        if (-not $proc.HasExited) {
            $proc.CloseMainWindow() | Out-Null
            if (-not $proc.WaitForExit($CloseWaitSec * 1000)) { $proc.Kill() }
        }
    } catch { }
    try { if (-not $stub.HasExited) { $stub.Kill() } } catch { }
}

# ---------------------------------------------------------------- report ---

Start-Sleep -Seconds 2
$after = Get-ChildItem (Join-Path $logs "*.log") -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($after) {
    Copy-Item -LiteralPath $after.FullName -Destination (Join-Path $capDir "run.log") -Force
}
$logText = Get-LogText (Join-Path $capDir "run.log")
$walkLines = @()
$pressLines = @()
$faultLines = @()
if ($logText) {
    $walkLines = ($logText -split "`r?`n") | Where-Object { $_ -match "store_catalogue: " }
    $pressLines = ($logText -split "`r?`n") | Where-Object { $_ -match "settings_bridge|asked for a fresh walk" }
    # Word-bounded on purpose: a loose `fault` also matches "default" (measured 2026-10-23, the first
    # run reported 15 "fault lines" that were all defaults).
    $faultLines = ($logText -split "`r?`n") | Where-Object { $_ -match "\bfault\b|Assert|crash" }
}
$stubText = Get-LogText $stubLog
$pressAsked = ($walkLines | Where-Object { $_ -match "\(a press asked\)" }).Count
$refusals = ($stubText -split "`r?`n" | Where-Object { $_ -match "^..:..:.. 503 " }).Count
$served = ($stubText -split "`r?`n" | Where-Object { $_ -match "^..:..:.. 200 " }).Count

$report = @()
$report += "probe_store_failure $stamp"
$report += "build        : $BuildDir"
$report += "api base     : http://127.0.0.1:$Port (stub, refusing then serving)"
$report += ""
$report += "--- verdicts ---"
foreach ($k in $verdict.Keys) { $report += ("{0,-14} {1}" -f $k, ($verdict[$k] -replace "`r?`n", " | ")) }
$report += ""
$report += "press-asked walks : $pressAsked"
$report += "stub refusals     : $refusals"
$report += "stub servings     : $served"
$report += "fault lines       : $($faultLines.Count)"
$report += ""
$report += "--- store_catalogue (host log) ---"
$report += $walkLines
$report += ""
$report += "--- the bridge's own lines ---"
$report += $pressLines
$report += ""
$report += "--- stub log ---"
$report += ($stubText -split "`r?`n")
$report | Set-Content -Encoding UTF8 (Join-Path $capDir "report.txt")
$report | Write-Host
Write-Host ""
Write-Host "probe_store_failure: evidence under $capDir"
