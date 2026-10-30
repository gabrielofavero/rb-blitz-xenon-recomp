# P4 of docs/plans/finished/2026-10-13-rhythmverse-store-plan.md: the search measurement.
#
# One boot that drives the store's own Search row and captures what the caption says. Since P9-11
# the typing happens on the **title's own keyboard**, not on authored key pages, so a scripted boot
# cannot press it; the probe therefore uses the seed the keyboard entry itself honours:
#
#   * `--store_search_seed=<query>` fills the host's field at boot without opening a surface
#     (src/store/store_search_service.cpp), and the store screen's `open_store_search` asks
#     `rbbz-store/search/seed` (src/store/store_search.h). While a seed is installed, pressing the
#     Search row submits the field instead of drawing the keyboard - the measurement switch, not a
#     feature: a player's boot never has one.
#   * `--store_trace=1` keeps the P2/P3 path probe on, so a run also records whether the store
#     asked for anything around the press.
#   * The search itself is real: the submit posts through src/store/store_http.cpp, and the worker's
#     answer is logged (`store_search: ...`) - the OCR of the caption is what proves the *player*
#     sees the field and the outcome.
#
# A scratch profile is used on purpose: the row that opens the store needs
# `general.rhythmverse = true`, and the developer's real `%APPDATA%\rb_blitz` profile must not be
# touched. Both `--user_data_root` and `--launcher_profile` are passed because the launcher profile
# outranks the user root for the settings file's location.
#
# Usage (execution policy is Restricted on this machine):
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_search.ps1 -Query bush
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_search.ps1 -Query zzzz
param(
    [string]$BuildDir = "out/build/win-amd64-release",
    [string]$OutDir = "out/_swap/search-probe",
    [string]$GameRoot,
    [string]$UltimateMode = "1",
    # The main menu row that opens the store. With the store's master switch on the measured order
    # is PLAY / PLAY CURRENT SONG / HELP & OPTIONS / EXIT GAME / RHYTHMVERSE (the row was
    # RHYTHMVERSE CUSTOMS until 2026-10-22).
    [int]$StoreRow = 4,
    [string]$Query = "bush",
    # A query already in the field at boot (the live-request arm: one Search press, no typing).
    # `-Query` seeds it when this is empty.
    [string]$Seed = "",
    # The keyboard's own arm: no seed is installed, so the Search press opens the title's keyboard.
    # The keyboard is the platform's ImGui dialog (the SDK implements `XamShowKeyboardUI`), so the
    # run proves the call reaches it; what a script cannot do is *type* on a console keyboard, which
    # is why every other arm seeds the field instead.
    [switch]$NoSeed,
    # `-NoSeed`'s typing route: driver key actions joined by commas (`key:m,key:a,key:s,key:s`
    # spells "mass" - `drive_ui.ps1` carries only a handful of letters), then `key:enter` as the
    # dialog's OK. Empty = open the keyboard and cancel it.
    [string]$TypeKeys = "key:m,key:a,key:s,key:s",
    # The store's own front menu row that runs the search: RECENT is 0, SEARCH 1 (P3's front menu).
    # The authored key pages this script used to drive were retired by P9-11, so a negative value is
    # refused below rather than silently opening the store and typing nothing.
    [int]$SearchMenuRow = 1,
    # How long to wait for the answer before Back is pressed on the keys page.
    [int]$SettleAfterAnswerSec = 15,
    # Control leg: open the store row (with `-SearchMenuRow 0` that is Recent) and capture what it
    # opened, without typing, submitting or going back. This is the leg that says the Search route
    # did not take Recent's press with it.
    [switch]$RowOnly,
    [string]$LogLevel = "debug",
    [int]$BootTimeoutSec = 180,
    [int]$ScreenTimeoutSec = 120,
    [int]$SettleAfterPressSec = 6,
    [int]$SettleAfterDoneSec = 20,
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

# The scratch profile: the store's master switch on, nothing else. The launcher file must be a
# *valid* document - an empty one fails to parse (`line 1: expected key = value`), and a failed
# profile read makes the boot rewrite settings.toml before the payload is built, which drops the
# switch this measurement depends on.
#
# The directory is shared across runs on purpose: it holds the rendered shader cache, and a cold
# one costs ~90 s before the title's first present. Only the two files are rewritten each run.
$profileDir = Join-Path $root (Join-Path $OutDir "profile")
New-Item -ItemType Directory -Force -Path $profileDir | Out-Null
# Windows PowerShell's `Set-Content -Encoding utf8` writes a BOM, and the TOML reader refuses a
# document whose first key carries one ("no schema_version", measured 2026-10-20). Write bytes.
$launcherText = @(
    "schema_version = 1",
    "",
    "[launcher]",
    "version = 1",
    "portable = false",
    "",
    "[launch]",
    "target = `"ultimate`"",
    ""
) -join "`r`n"
[System.IO.File]::WriteAllText((Join-Path $profileDir "launcher.toml"), $launcherText,
                               (New-Object System.Text.UTF8Encoding($false)))
$settingsText = @(
    "schema_version = 1",
    "",
    "[general]",
    "rhythmverse = true",
    "",
    "[interface]",
    "hide_main_menu = true",
    "hidden_main_menu = ['splash_leaderboard', 'splash_achievements']",
    ""
) -join "`r`n"
[System.IO.File]::WriteAllText((Join-Path $profileDir "settings.toml"), $settingsText,
                               (New-Object System.Text.UTF8Encoding($false)))
# An empty DLC folder keeps the discovery short: the owner's real library costs minutes with a cold
# cache, and the search does not need it. An existing directory is used because an empty `--flag=`
# makes the SDK's parser take the *next* argument as the value (measured, docs/settings/menu.md).
$emptyDlc = Join-Path $profileDir "empty-dlc"
New-Item -ItemType Directory -Force -Path $emptyDlc | Out-Null

# ---------------------------------------------------------------- helpers ---

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

# The store route is the only route since P9-11: the Search row opens the title's keyboard, and a
# script cannot type on it. The seeded field is what lets this probe reach the request at all.
if ($SearchMenuRow -lt 0) {
    throw "the authored key pages are retired (P9-11); pass -SearchMenuRow 1 for the store's Search row"
}
if ($Seed -eq "") { $Seed = $Query }
if ($NoSeed) { $Seed = "" }

$clampUp = @()
for ($i = 0; $i -lt 8; $i++) { $clampUp += @("key:lstick_up", "wait:1") }

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
    "--store_trace=1",
    "--user_data_root=$profileDir",
    "--launcher_profile=$(Join-Path $profileDir 'launcher.toml')",
    "--dlc_library=$emptyDlc",
    "--dlc_root=$emptyDlc"
)
# The diagnostic jump is retired with the key pages; the store route always runs.
# The seed travels only when the run wants one: `-NoSeed` leaves `store_search_seed` empty so the
# screen's `open_store_search` draws the keyboard instead of submitting.
if (-not $NoSeed) { $arguments += "--store_search_seed=$Seed" }

Write-Host "probe_search: launching $exe"
Write-Host "  args: $($arguments -join ' ')"
Write-Host "  query: '$Query'"
$proc = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $arguments

$verdict = [ordered]@{
    stamp      = $stamp
    query      = $Query
    store_row  = $StoreRow
    search_row = $SearchMenuRow
    window     = $false
    title_text = ""
    menu_text  = ""
    row_text   = ""
    store_text = ""
    keys_text  = ""
    typed_text = ""
    done_text  = ""
    answer_text = ""
    back_text   = ""
    results_text = ""
    page_text    = ""
}

try {
    $verdict.window = Wait-ForWindow $proc $BootTimeoutSec
    if (-not $verdict.window) { throw "no game window after ${BootTimeoutSec}s" }

    $verdict.title_text = Wait-ForNeedle (Join-Path $capDir "title.png") "TO START" $BootTimeoutSec "title"
    Invoke-Actions @("key:a", "wait:8", "wait:6")
    $verdict.menu_text = Wait-ForNeedle (Join-Path $capDir "menu.png") "PLAY" $ScreenTimeoutSec "main menu"

    $route = @($clampUp)
    for ($d = 0; $d -lt $StoreRow; $d++) { $route += @("key:lstick_down", "wait:1") }
    Invoke-Actions $route
    Capture-To (Join-Path $capDir "row.png")
    $verdict.row_text = Get-ScreenText (Join-Path $capDir "row.png")
    Invoke-Actions @("key:a")
    Start-Sleep -Seconds $SettleAfterPressSec

    if ($SearchMenuRow -ge 0) {
        # The store's own front menu, then its Search row (RECENT 0, SEARCH 1): the press that the
        # payload's gate turns into the keys page while no answer is waiting.
        $verdict.store_text = Wait-ForNeedle (Join-Path $capDir "store.png") "RECENT" `
            $ScreenTimeoutSec "store menu"
        $toSearch = @($clampUp)
        for ($d = 0; $d -lt $SearchMenuRow; $d++) { $toSearch += @("key:lstick_down", "wait:1") }
        Invoke-Actions $toSearch
        Invoke-Actions @("key:a")
        if ($RowOnly) {
            Capture-To (Join-Path $capDir "page.png")
            $verdict.page_text = Get-ScreenText (Join-Path $capDir "page.png")
        } elseif ($NoSeed) {
            # The keyboard's own arm: the press must have drawn the title's keyboard over the store
            # menu. The keyboard is the platform's ImGui dialog, so the host keys the driver can send
            # type into it; Enter is the dialog's OK (`InputText` returns true on Enter). A run that
            # lands the text proves the whole path - dialog, `$ok`/`$text` back to the screen, the
            # `text/<query>` write and the submit.
            Capture-To (Join-Path $capDir "keys.png")
            $verdict.keys_text = Get-ScreenText (Join-Path $capDir "keys.png")
            if ($TypeKeys) {
                $typing = @()
                foreach ($k in ($TypeKeys -split ',')) { $typing += @($k.Trim(), "wait:1") }
                $typing += @("wait:2")
                Invoke-Actions $typing
                Capture-To (Join-Path $capDir "typed-on-keyboard.png")
                $verdict.typed_text = Get-ScreenText (Join-Path $capDir "typed-on-keyboard.png")
                Invoke-Actions @("key:enter", "wait:2")
                Start-Sleep -Seconds $SettleAfterPressSec
                Capture-To (Join-Path $capDir "answer.png")
                $verdict.answer_text = Get-ScreenText (Join-Path $capDir "answer.png")
            }
            foreach ($attempt in 1..3) {
                Invoke-Actions @("key:b", "wait:2")
                Start-Sleep -Seconds 2
            }
            Capture-To (Join-Path $capDir "back.png")
            $verdict.back_text = Get-ScreenText (Join-Path $capDir "back.png")
        } else {
            # The press submits the seeded query (the screen's `seed` branch) *and* asks the store
            # for the search page in the same breath (`store::AddSearchKeyboard`), so the browser
            # opens on the answer with no second press. The host holds that request until the first
            # page of the answer lands (`store_search_wait_ms`), which is the wait this leg sits
            # through.
            Start-Sleep -Seconds $SettleAfterPressSec
            Capture-To (Join-Path $capDir "keys.png")
            $verdict.keys_text = Get-ScreenText (Join-Path $capDir "keys.png")
            Start-Sleep -Seconds $SettleAfterAnswerSec
            Capture-To (Join-Path $capDir "results.png")
            $verdict.results_text = Get-ScreenText (Join-Path $capDir "results.png")
        }
    } else {
        throw "the Search row must be the store's own (>= 0); the authored key pages are retired"
    }
} finally {
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

$searchLines = @()
$probeLines = @()
$menuLines = @()
if ($logText) {
    $searchLines = ($logText -split "`r?`n") | Where-Object { $_ -match "store_search:" }
    $probeLines = ($logText -split "`r?`n") | Where-Object { $_ -match "store-probe:" }
    $menuLines = ($logText -split "`r?`n") | Where-Object { $_ -match "settings_menu: (the search|the store's search)" }
}

$report = @()
$report += "probe_search $stamp"
$report += "build        : $BuildDir"
$report += "query        : '$Query'"
$report += "store row    : $StoreRow"
$report += ""
$report += "--- main menu (OCR) ---"
$report += $verdict.menu_text
$report += ""
$report += "--- selected row (OCR) ---"
$report += $verdict.row_text
$report += ""
if ($SearchMenuRow -ge 0) {
    $report += "--- the retail store menu (OCR) ---"
    $report += $verdict.store_text
    $report += ""
}
if ($RowOnly) {
    $report += "--- the store row's page (OCR) ---"
    $report += $verdict.page_text
    $report += ""
}
$report += "--- the keyboard entry: the store menu after the Search press (OCR) ---"
$report += $verdict.keys_text
$report += ""
if (-not $NoSeed) {
    $report += "--- after the submit, settle ${SettleAfterAnswerSec}s: the search page the press opened (OCR) ---"
    $report += $verdict.results_text
    $report += ""
}
$report += "--- store_search lines ($($searchLines.Count)) ---"
$report += $searchLines
$report += ""
$report += "--- store-probe lines ($($probeLines.Count)) ---"
$report += $probeLines
$report += ""
$report += "--- payload notes ($($menuLines.Count)) ---"
$report += $menuLines

$reportPath = Join-Path $capDir "report.txt"
$report | Set-Content -LiteralPath $reportPath -Encoding utf8
$report | Write-Host

Write-Host ""
Write-Host "probe_search: wrote $reportPath"
