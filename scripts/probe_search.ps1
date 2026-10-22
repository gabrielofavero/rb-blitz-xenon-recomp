# P4 of docs/plans/finished/2026-10-13-rhythmverse-store-plan.md: the search measurement.
#
# One boot that drives the authored key pages end to end and captures what the caption says:
#
#   * `--store_search_diagnostic=1` (src/store/store_search_service.cpp) appends
#     `{ui goto_screen settings_rhythmverse_search_screen}` to the retail store screen's own
#     `{enter}`, so the boot reaches the keys without P3's front menu having landed. It is a
#     measurement switch, not a feature.
#   * `--store_trace=1` keeps the P2/P3 path probe on, so a run also records whether the store
#     asked for anything while the keys were up.
#   * The search itself is real: Done posts through src/store/store_http.cpp, and the worker's
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
    # is PLAY / PLAY CURRENT SONG / HELP & OPTIONS / EXIT GAME / RHYTHMVERSE CUSTOMS.
    [int]$StoreRow = 4,
    [string]$Query = "bush",
    # A query already in the field at boot (the live-request arm: one Done press, no typing).
    [string]$Seed = "",
    # P4's delivered route (store_payload.h `RouteSearchRowToKeys`): >= 0 enters the retail store
    # from `-StoreRow`, then opens the **served front menu's** Search row at this index (RECENT is 0,
    # SEARCH 1), instead of the main-menu diagnostic jump. The rest of the route is the delivered
    # one: Done submits and stays, Back leaves the keys, and pressing Search again asks for the
    # page the answer fills - which is what puts the results in the retail browser. -1 keeps the
    # old diagnostic route (the keys straight from the main menu).
    [int]$SearchMenuRow = -1,
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

# The key route for `query`: the pages are the authored table's, so the script derives the path
# instead of hard-coding one. The root's first row is Done and every character page returns to the
# root, so the route is flat: for each character, Letters -> its page -> its letter, then Back; and
# Done is one press at the end.
function Get-KeyRoute([string]$Text) {
    $groups = @("abcdef", "ghijkl", "mnopqr", "stuvwx", "yz")
    $route = @()
    foreach ($c in $Text.ToLowerInvariant().ToCharArray()) {
        $page = -1
        $row = -1
        for ($p = 0; $p -lt $groups.Count; $p++) {
            $at = $groups[$p].IndexOf($c)
            if ($at -ge 0) { $page = $p; $row = $at; break }
        }
        if ($page -lt 0) { throw "the key pages do not carry '$c'" }
        # The root's rows are Done, Letters, Numbers 0-5, Numbers 6-9, Space, More Keys.
        $route += @("key:lstick_down", "wait:1")  # Done -> Letters
        $route += @("key:a", "wait:2")            # the letters index (row 0 = "Letters A-F")
        for ($i = 0; $i -lt $page; $i++) { $route += @("key:lstick_down", "wait:1") }
        $route += @("key:a", "wait:2")            # the index -> the letter page
        for ($i = 0; $i -lt $row; $i++) { $route += @("key:lstick_down", "wait:1") }
        $route += @("key:a", "wait:1")            # append the character
        $route += @("key:b", "wait:2")            # the page -> the root (Done is row 0)
    }
    $route += @("key:a")                           # Done
    return $route
}

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
    "--store_search_seed=$Seed",
    "--user_data_root=$profileDir",
    "--launcher_profile=$(Join-Path $profileDir 'launcher.toml')",
    "--dlc_library=$emptyDlc",
    "--dlc_root=$emptyDlc"
)
# The diagnostic jump is a measurement switch; the store route must run without it, or the main
# menu's row would open the keys instead of the store.
if ($SearchMenuRow -lt 0) { $arguments += "--store_search_diagnostic=1" }

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
        } else {
            $verdict.keys_text = Wait-ForNeedle (Join-Path $capDir "keys.png") "LETTERS" `
                $ScreenTimeoutSec "the authored key pages"
        }
    } else {
        Capture-To (Join-Path $capDir "keys.png")
        $verdict.keys_text = Get-ScreenText (Join-Path $capDir "keys.png")
    }

    # `-RowOnly` stops here: the press was the measurement.
    if (-not $RowOnly) {
    # With a seeded field (`-Seed`), the root's first row is Done: one press exercises the request
    # and the answer. Without a seed the query is typed on the key pages.
    if ($Seed -ne "") {
        Invoke-Actions @("key:a")
        # The diagnostic route's second press re-submitted the same query; the store route does not
        # need it (Done stays on the page and the strip reports the answer there).
        if ($SearchMenuRow -lt 0) { Invoke-Actions @("key:a") }
    } else {
        Invoke-Actions (Get-KeyRoute $Query)
    }
    Start-Sleep -Seconds 2
    Capture-To (Join-Path $capDir "typed.png")
    $verdict.typed_text = Get-ScreenText (Join-Path $capDir "typed.png")

    Start-Sleep -Seconds $SettleAfterAnswerSec
    Capture-To (Join-Path $capDir "answer.png")
    $verdict.answer_text = Get-ScreenText (Join-Path $capDir "answer.png")

    if ($SearchMenuRow -ge 0) {
        # Back returns to the store menu (the keys screen's own back target), and the same Search
        # row now asks for the page the answer fills: the retail browser, listing the results.
        Invoke-Actions @("key:b", "wait:3")
        $verdict.back_text = Wait-ForNeedle (Join-Path $capDir "store-back.png") "RECENT" `
            $ScreenTimeoutSec "store menu after Back"
        $toResults = @($clampUp)
        for ($d = 0; $d -lt $SearchMenuRow; $d++) { $toResults += @("key:lstick_down", "wait:1") }
        Invoke-Actions $toResults
        Invoke-Actions @("key:a")
        Start-Sleep -Seconds $SettleAfterPressSec
        Capture-To (Join-Path $capDir "results.png")
        $verdict.results_text = Get-ScreenText (Join-Path $capDir "results.png")
    }
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
$report += "--- the authored key pages (OCR) ---"
$report += $verdict.keys_text
$report += ""
$report += "--- after typing '$Query' (OCR) ---"
$report += $verdict.typed_text
$report += ""
$report += "--- after Done, settle ${SettleAfterAnswerSec}s (OCR) ---"
$report += $verdict.answer_text
$report += ""
if ($SearchMenuRow -ge 0) {
    $report += "--- store menu after Back (OCR) ---"
    $report += $verdict.back_text
    $report += ""
    $report += "--- the Search row again: the answer as the retail list (OCR) ---"
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
