# P9-2 of docs/plans/finished/2026-10-13-rhythmverse-store-plan.md: the master switch's
# restart rule, measured in two boots.
#
# The owner pass (2026-10-22) made `general.rhythmverse` **restart**-scoped, because the switch owns
# what the boot drew as well as what the store's workers read: the main-menu *RhythmVerse*
# row is compiled into the record the payload generates and the store's heading is a locale entry
# that payload replaces. The five instrument leaves stay `hot`.
#
# Boot 1 (session) walks Help & Options with the mouse-on-row-bounds route the acceptance harness
# measured (a click's screen is the oracle):
#
#   * Downloadable Content -> RhythmVerse Song Filter -> Guitar flipped Off: the leaf is applied
#     live (`hot_apply: general.rhythmverse_guitar = false`), and leaving Help & Options draws **no**
#     restart prompt.
#   * Re-enter, flip the **master** Off, leave again: the restart confirmation appears exactly once
#     (`settings_restart_screen`), the log carries **no** `hot_apply: general.rhythmverse` line, and
#     no catalogue request is made by the commit (the boot's own prewarm count does not move).
#   * `Close` finishes the leave on the pre-change value: the main menu still draws the store row.
#   * The profile carries both committed values (`rhythmverse = false`, `rhythmverse_guitar =
#     false`), which is what the next boot acts on.
#
# Boot 2 (next boot, the same profile) is the value acting: no store row on the main menu, and no
# `store_catalogue` / `store_fetch` / `store_library` / `store_download` / `store_search` request -
# the switch's silence, reached through the UI instead of a hand-written settings file.
#
# Usage (this machine's execution policy is Restricted):
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_master_restart.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\probe_master_restart.ps1 -SessionOnly
param(
    [string]$BuildDir = 'out/build/win-amd64-release',
    [string]$OutDir = 'out/_swap/master-restart',
    [string]$GameRoot,
    [string]$UltimateMode = '1',
    [switch]$SessionOnly,
    # Re-run only the second boot, on the `settings.toml` the session leg already committed. The
    # fixture is then **not** rewritten - the point of the leg is the value the game itself wrote.
    [switch]$NextBootOnly,
    [int]$BootTimeoutSec = 240,
    [int]$ScreenTimeoutSec = 120,
    [int]$NextBootTimeoutSec = 420,
    [int]$CloseWaitSec = 25
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work 'rb_blitz.exe'
if (-not $GameRoot) { $GameRoot = Join-Path $root 'game' }
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$base = Join-Path $root (Join-Path $OutDir $stamp)
$shots = Join-Path $base 'shots'
# `drive_ui.ps1` joins its `-OutDir` against the repository root, so it is handed the root-relative
# spelling; capture/OCR take absolute paths.
$shotsRel = $shots
if ($shots.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    $shotsRel = $shots.Substring($root.Length).TrimStart('\', '/')
}

if (-not (Test-Path -LiteralPath $exe)) { throw "missing $exe" }
if (-not (Test-Path -LiteralPath (Join-Path $GameRoot 'default.xex'))) { throw 'no default.xex under $GameRoot' }
if (Get-Process rb_blitz -ErrorAction SilentlyContinue) { throw 'Close the running game first.' }
New-Item -ItemType Directory -Force -Path $base, $shots | Out-Null

# The scratch profile is shared across runs on purpose (it holds the rendered shader cache; a cold
# one costs ~90 s), and only its two files are rewritten each run. It never names the master, so the
# session boot starts on the compiled default (On).
$profileDir = Join-Path $root (Join-Path $OutDir 'profile')
$emptyDlc = Join-Path $profileDir 'empty-dlc'
New-Item -ItemType Directory -Force -Path $profileDir, $emptyDlc | Out-Null

$script:failures = @()
$script:steps = 0
$script:currentLog = ''
$script:logText = ''

# ---------------------------------------------------------------- fixtures ---

function Write-TextFile([string]$Path, [string[]]$Lines) {
    $text = ($Lines -join "`r`n") + "`r`n"
    [System.IO.File]::WriteAllText($Path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

Write-TextFile (Join-Path $profileDir 'launcher.toml') @(
    'schema_version = 1',
    '',
    '[launcher]',
    'version = 1',
    'portable = false',
    '',
    '[launch]',
    'target = "ultimate"'
)
if (-not $NextBootOnly) {
    Write-TextFile (Join-Path $profileDir 'settings.toml') @(
        'schema_version = 1',
        '',
        '[general]',
        'refresh_music_library = true',
        '',
        '[interface]',
        'hide_main_menu = true',
        "hidden_main_menu = ['splash_leaderboard', 'splash_achievements']"
    )
}

# ---------------------------------------------------------------- helpers ---

function Invoke-ChildScript([string]$Script, [hashtable]$Named) {
    $argv = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot $Script))
    foreach ($k in $Named.Keys) { $argv += @("-$k", [string]$Named[$k]) }
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & powershell @argv 2>&1 | ForEach-Object { "$_" }
    } finally {
        $ErrorActionPreference = $previous
    }
    if ($LASTEXITCODE -ne 0) { throw "$Script failed: $($out -join ' ')" }
    return $out
}

function Drive {
    param([string[]]$Actions)
    & (Join-Path $PSScriptRoot 'drive_ui.ps1') -Actions $Actions -BuildDir $BuildDir -OutDir $shotsRel | Out-Null
}

function Save-Shot([string]$Name) {
    $path = Join-Path $shots $Name
    for ($i = 1; $i -le 3; $i++) {
        try { Invoke-ChildScript 'capture_window.ps1' @{ OutFile = $path } | Out-Null; return $path }
        catch { if ($i -eq 3) { throw }; Start-Sleep -Seconds 2 }
    }
}

function Get-ScreenText([string]$Name) {
    $path = Join-Path $shots $Name
    Save-Shot $Name | Out-Null
    return ((Invoke-ChildScript 'ocr_image.ps1' @{ Path = $path }) -join ' ').ToUpperInvariant()
}

function Wait-ForScreen([string]$Name, [string]$Needle, [int]$TimeoutSec, [string]$Label) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = ''
    while ((Get-Date) -lt $deadline) {
        try { $last = Get-ScreenText $Name }
        catch { Start-Sleep -Seconds 3; continue }
        if ($Needle -eq '' -or $last.Contains($Needle.ToUpperInvariant())) { return $last }
        Start-Sleep -Seconds 3
    }
    Write-Host "  !! timed out after ${TimeoutSec}s waiting for '$Needle' ($Label); last text: $last"
    return $last
}

function Get-FileText([string]$Path) {
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return '' }
    for ($i = 0; $i -lt 5; $i++) {
        try { return (Get-Content -LiteralPath $Path -Raw) } catch { Start-Sleep -Milliseconds 200 }
    }
    return ''
}

function Refresh-LogText { $script:logText = Get-FileText $script:currentLog }

function Get-LogMatches([string]$Pattern) {
    Refresh-LogText
    return ([regex]::Matches($script:logText, $Pattern)).Count
}

function Get-Screen {
    $lines = Select-String -Path $script:currentLog -Pattern "native_mouse: screen=(\S+)|help_observer: screen '([^']+)'" -ErrorAction SilentlyContinue
    if (-not $lines) { return '' }
    $m = [regex]::Match($lines[-1].Line, "native_mouse: screen=(\S+)|help_observer: screen '([^']+)'")
    if ($m.Groups[1].Success) { return $m.Groups[1].Value }
    return $m.Groups[2].Value
}

function Wait-ForScreenName([string]$Expected, [int]$TimeoutSec, [string]$Label) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $last = ''
    while ((Get-Date) -lt $deadline) {
        $last = Get-Screen
        if ($last -eq $Expected) { return $true }
        Start-Sleep -Seconds 1
    }
    Write-Host "  !! timed out waiting for screen '$Expected' ($Label); the log says '$last'"
    return $false
}

# The host's own row table for the focused list (native_mouse row lines), newest run first.
function Get-RowTable {
    param([int]$Index)
    $lines = Select-String -Path $script:currentLog -Pattern 'native_mouse: row=(\d+) bounds=' -ErrorAction SilentlyContinue
    if (-not $lines) { return $null }
    $runs = @()
    $current = @()
    $last = -1
    foreach ($line in $lines) {
        $m = [regex]::Match($line.Line, 'row=(\d+) bounds=([0-9.]+),([0-9.]+),([0-9.]+),([0-9.]+)')
        if (-not $m.Success) { continue }
        $n = [int]$m.Groups[1].Value
        if ($n -le $last) { if ($current.Count) { $runs += , $current }; $current = @() }
        $last = $n
        $current += , @($n, [double]$m.Groups[2].Value, [double]$m.Groups[3].Value,
                        [double]$m.Groups[4].Value, [double]$m.Groups[5].Value)
    }
    if ($current.Count) { $runs += , $current }
    for ($i = $runs.Count - 1; $i -ge 0; $i--) {
        $hit = $runs[$i] | Where-Object { $_[0] -eq $Index }
        if ($hit) { return $hit }
    }
    return $null
}

function Wait-ForRowTable([int]$Index, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) {
        $hit = Get-RowTable $Index
        if ($hit) { return $hit }
        Start-Sleep -Seconds 1
    }
    return $null
}

function ClickRow {
    param([int]$Index, [string]$Tag, [double]$Wait = 3, [int]$RowTimeoutSec = 25)
    # A hover at the list is what makes the native mouse capture the row table (two seconds, no OCR).
    Drive @('mouse:0.5:0.78', 'wait:2')
    $hit = Wait-ForRowTable $Index $RowTimeoutSec
    if (-not $hit) { Write-Host "  !! ${Tag}: no row $Index in the table"; return $false }
    $x = [math]::Round((($hit[1] + $hit[3]) / 2), 4)
    $y = [math]::Round((($hit[2] + $hit[4]) / 2), 4)
    Write-Host ("  {0}: row {1} at {2},{3}" -f $Tag, $Index, $x, $y)
    Drive @("mouse:$x`:$y", 'wait:1', 'click:left', "wait:$Wait")
    return $true
}

function Test-MenuUp {
    if ((Get-Screen) -ne 'splash_screen') { return $false }
    if ((Get-LogMatches 'content_pipeline: usable menu') -gt 0) { return $true }
    return ($null -ne (Get-RowTable 0))
}

function Enter-MainMenu([int]$BootTimeout) {
    $title = Wait-ForScreen 'title.png' 'TO START' $BootTimeout 'title'
    Add-Step 'boot: the title screen is drawn' ($title.Contains('TO START'))
    $presses = 0
    $deadline = (Get-Date).AddSeconds([Math]::Max(300, $BootTimeout))
    while ((Get-Date) -lt $deadline) {
        Drive @('mouse:0.5:0.78', 'wait:1')
        if (Test-MenuUp) { break }
        if ($presses -ge 40) { break }
        $presses += 1
        Drive @('key:a', 'wait:12')
    }
    if (-not (Test-MenuUp)) {
        Add-Step 'boot: the main menu is up' $false "$(Get-Screen) after $presses accept(s)"
        return $false
    }
    Drive @('mouse:0.5:0.78', 'wait:2')
    $rows = Wait-ForRowTable 0 60
    Add-Step 'boot: the main menu is up' ($null -ne $rows) "$presses accept(s)"
    return ($null -ne $rows)
}

function Test-MainMenu {
    if ((Get-Screen) -eq 'splash_screen' -and $null -ne (Get-RowTable 0)) { return $true }
    return ((Get-Screen) -eq 'splash_screen')
}

function Return-ToMenu {
    for ($i = 0; $i -lt 4; $i++) {
        if (Test-MainMenu) { return $true }
        Drive @('key:b', 'wait:2')
    }
    return (Test-MainMenu)
}

function Open-ScreenByRow {
    param([string]$Expected, [int]$Hint, [string]$Tag)
    if (-not (Return-ToMenu)) {
        Write-Host "  !! ${Tag}: the main menu could not be reached before the row scan"
        return -1
    }
    for ($attempt = 0; $attempt -lt 8; $attempt++) {
        $index = ($Hint + $attempt) % 8
        if (-not (Get-RowTable $index)) { continue }
        if (-not (ClickRow $index "$Tag-row$index" -Wait 2 -RowTimeoutSec 8)) { continue }
        if (Wait-ForScreenName $Expected 15 "$Tag row $index") { return $index }
        Return-ToMenu | Out-Null
    }
    return -1
}

function Add-Step([string]$Name, [bool]$Ok, [string]$Detail = '') {
    $script:steps += 1
    $shown = if ($Detail.Length -gt 240) { $Detail.Substring(0, 240) } else { $Detail }
    if ($Ok) {
        Write-Host ("  ok   {0}{1}" -f $Name, $(if ($shown) { ": $shown" } else { '' }))
    } else {
        $script:failures += ("{0}{1}" -f $Name, $(if ($shown) { ": $shown" } else { '' }))
        Write-Host ("  FAIL {0}{1}" -f $Name, $(if ($shown) { ": $shown" } else { '' }))
    }
}

function Assert-Label([string]$Text, [string[]]$Words, [string]$Name) {
    $normalized = $Text.ToUpperInvariant() -replace '[^A-Z0-9]', ''
    $missing = @()
    foreach ($word in $Words) {
        $needle = [regex]::Replace($word.ToUpperInvariant(), '[^A-Z0-9]', '')
        if (-not $normalized.Contains($needle)) { $missing += $needle }
    }
    Add-Step $Name ($missing.Count -eq 0) "missing [$($missing -join ', ')] of '$(($Words -join ' '))'"
}

# The boolean form of the same fold, for a decision the route makes on what it is looking at.
function Test-Label([string]$Text, [string[]]$Words) {
    $normalized = $Text.ToUpperInvariant() -replace '[^A-Z0-9]', ''
    foreach ($word in $Words) {
        if (-not $normalized.Contains([regex]::Replace($word.ToUpperInvariant(), '[^A-Z0-9]', ''))) {
            return $false
        }
    }
    return $true
}

# ---------------------------------------------------------------- process ---

function Start-Boot([string]$Name, [int]$BootTimeout) {
    $logPath = Join-Path $base "$Name.log"
    if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath -Force }
    $script:currentLog = $logPath
    $script:logText = ''
    $arguments = @(
        "--game_data_root=""$GameRoot""",
        "--ultimate_mode=$UltimateMode",
        '--mnk_mode=1',
        "--user_data_root=""$profileDir""",
        "--launcher_profile=""$(Join-Path $profileDir 'launcher.toml')""",
        "--dlc_root=""$emptyDlc""",
        "--log_file=""$logPath""",
        '--log_level=info',
        '--log_flush_interval=1',
        '--log_max_file_size_mb=200',
        '--mouse_native_trace=true'
    )
    Write-Host ''
    Write-Host "=== $Name ==="
    Write-Host "  args: $($arguments -join ' ')"
    $proc = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $arguments
    $deadline = (Get-Date).AddSeconds($BootTimeout)
    while ((Get-Date) -lt $deadline) {
        try {
            $proc.Refresh()
            if ($proc.HasExited) { break }
            if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { return $proc }
        } catch { }
        Start-Sleep -Seconds 2
    }
    throw "$Name`: no game window after ${BootTimeout}s"
}

function Stop-Boot($Proc) {
    try {
        if (-not $Proc.HasExited) {
            $Proc.CloseMainWindow() | Out-Null
            if (-not $Proc.WaitForExit($CloseWaitSec * 1000)) { Stop-Process -Id $Proc.Id -Force }
        }
    } catch { }
    Start-Sleep -Seconds 2
    Refresh-LogText
}

# Row indices, as the acceptance harness measured them on 2026-10-22: the merged root is How to
# Play 0 / Calibration 1 / General 2, the General page is Target 0 / Interface 1 / Downloadable
# Content 2, and the Downloadable Content page is Cache 0 / Refresh 1 / Scan Threads 2 /
# RhythmVerse 3 / RhythmVerse Song Filter 4 / DLC Menu Music 5.
$HelpOptionsRow = 1
$HelpGeneralRow = 2
$GeneralDlcRow = 2
$RhythmVerseRow = 3
$FilterRow = 4

# ------------------------------------------------------------ boot 1: session ---

function Invoke-SessionLeg {
    $proc = Start-Boot 'session' $BootTimeoutSec
    try {
    if (-not (Enter-MainMenu $BootTimeoutSec)) { return }

    # --- the filter leaf: hot, and leaving draws no prompt --------------------------------
    if ((Open-ScreenByRow 'settings_screen' $HelpOptionsRow 'help-options') -lt 0) {
        Add-Step 'session: Help & Options opens' $false (Get-Screen)
        return
    }
    Add-Step 'session: Help & Options opens' $true 'settings_screen'
    if (-not (ClickRow $HelpGeneralRow 'general') -or
        -not (Wait-ForScreenName 'settings_general_screen' 15 'general')) {
        Add-Step 'session: General opens' $false (Get-Screen)
        return
    }
    if (-not (ClickRow $GeneralDlcRow 'dlc') -or
        -not (Wait-ForScreenName 'settings_dlc_screen' 15 'dlc page')) {
        Add-Step 'session: Downloadable Content opens' $false (Get-Screen)
        return
    }
    $before = Wait-ForScreen 'dlc-before.png' 'RHYTHMVERSE' $ScreenTimeoutSec 'dlc page'
    Assert-Label $before @('RHYTHMVERSE', 'ON') 'session: the master reads On before anything is touched'

    if (-not (ClickRow $FilterRow 'filter') -or
        -not (Wait-ForScreenName 'settings_rhythmverse_filter_screen' 15 'filter page')) {
        Add-Step 'session: the filter page opens' $false (Get-Screen)
        return
    }
    $filterBefore = Get-ScreenText 'filter-before.png'
    Assert-Label $filterBefore @('GUITAR') 'session: the filter page draws Guitar'
    ClickRow 0 'guitar' | Out-Null
    $filterAfter = Get-ScreenText 'filter-after.png'
    Assert-Label $filterAfter @('GUITAR', 'OFF') 'session: Guitar flipped Off on the page'

    # filter -> DLC -> General -> root -> leave. A leaf change must not ask for a restart.
    Drive @('key:b', 'wait:2', 'key:b', 'wait:2', 'key:b', 'wait:2', 'key:b', 'wait:3')
    $leftAfterLeaf = Wait-ForScreenName 'splash_screen' 25 'leave after a leaf'
    Add-Step 'session: leaving after a filter leaf returns to the main menu' $leftAfterLeaf (Get-Screen)
        Add-Step 'session: a filter leaf asks for no restart' ((Get-LogMatches 'settings_restart_screen') -eq 0) "$((Get-LogMatches 'settings_restart_screen')) restart prompt line(s)"
    Assert-Label (Get-ScreenText 'menu-after-leaf.png') @('CUST') 'session: the menu still carries the store row'

    # --- the master: restart-scoped, and the prompt is what says so -----------------------
    if ((Open-ScreenByRow 'settings_screen' $HelpOptionsRow 'help-options-2') -lt 0) {
        Add-Step 'session: Help & Options opens again' $false (Get-Screen)
        return
    }
    ClickRow $HelpGeneralRow 'general-2' | Out-Null
    if (-not (Wait-ForScreenName 'settings_general_screen' 15 'general-2')) {
        Add-Step 'session: General opens again' $false (Get-Screen); return
    }
    ClickRow $GeneralDlcRow 'dlc-2' | Out-Null
    if (-not (Wait-ForScreenName 'settings_dlc_screen' 15 'dlc page 2')) {
        Add-Step 'session: Downloadable Content opens again' $false (Get-Screen); return
    }
    $fetchesBefore = Get-LogMatches 'store_catalogue: fetching'
    ClickRow $RhythmVerseRow 'rhythmverse' | Out-Null
    $dlcAfter = Get-ScreenText 'dlc-after.png'
    Assert-Label $dlcAfter @('RHYTHMVERSE', 'OFF') 'session: the master flipped Off on the page'

    Drive @('key:b', 'wait:2', 'key:b', 'wait:2', 'key:b', 'wait:2')
    $prompt = Wait-ForScreenName 'settings_restart_screen' 25 'restart prompt'
    Add-Step 'session: leaving after a master change draws the restart prompt' $prompt (Get-Screen)
    # The prompt is logged once per entry as `native_mouse: screen=settings_restart_screen …
    # available=true`; the exit logs the same name with available=false, which is why the count is
    # of *entries*. Comparing the count after Close is what proves the leave drew it once.
    $promptEntries = Get-LogMatches 'native_mouse: screen=settings_restart_screen .*available=true'
    $promptText = Get-ScreenText 'prompt.png'
    Assert-Label $promptText @('RESTART', 'CLOSE') 'session: the prompt offers Restart now and Close'

    # Close is the first row and the one selected on entry: it finishes the leave.
    Drive @('key:a', 'wait:3')
    $closed = Wait-ForScreenName 'splash_screen' 25 'close'
    Add-Step 'session: Close finishes the leave' $closed (Get-Screen)
    Assert-Label (Get-ScreenText 'menu-after-close.png') @('CUST') 'session: the store row is still drawn - the session kept its pre-change value'

    # --- the host-side record ------------------------------------------------------------
    Refresh-LogText
    Add-Step 'session: the master is never applied live' ((Get-LogMatches 'hot_apply: general\.rhythmverse =') -eq 0) "$((Get-LogMatches 'hot_apply: general\.rhythmverse =')) hot apply line(s)"
    Add-Step 'session: the filter leaf is applied live' ((Get-LogMatches 'hot_apply: general\.rhythmverse_guitar =') -ge 1) "$((Get-LogMatches 'hot_apply: general\.rhythmverse_guitar =')) hot apply line(s)"
    $promptEntriesEnd = Get-LogMatches 'native_mouse: screen=settings_restart_screen .*available=true'
    Add-Step 'session: the prompt was entered once, and Close did not enter it again' (($promptEntries -eq 1) -and ($promptEntriesEnd -eq $promptEntries)) "$promptEntries entry at the prompt, $promptEntriesEnd at the end"
    $fetchesAfter = Get-LogMatches 'store_catalogue: fetching'
    Add-Step 'session: the master commit made no catalogue request' ($fetchesAfter -eq $fetchesBefore) "fetch lines before=$fetchesBefore after=$fetchesAfter"
    Add-Step 'session: the store was never opened' ((Get-LogMatches "store_fetch: served '") -eq 0) "$((Get-LogMatches "store_fetch: served '")) served record(s)"
    $settingsText = Get-FileText (Join-Path $profileDir 'settings.toml')
    Copy-Item -LiteralPath (Join-Path $profileDir 'settings.toml') -Destination (Join-Path $base 'settings-after-session.toml') -Force
    Add-Step 'session: the master was committed durably' ($settingsText -match '(?m)^\s*rhythmverse\s*=\s*false') ($settingsText -replace "`r?`n", ' | ')
    Add-Step 'session: the filter leaf was committed durably' ($settingsText -match '(?m)^\s*rhythmverse_guitar\s*=\s*false') ''
    } finally {
        Stop-Boot $proc
    }
}

# ------------------------------------------------------------ boot 2: next start ---

function Invoke-NextBootLeg {
    $proc = Start-Boot 'next-boot' $NextBootTimeoutSec
    try {
        if (-not (Enter-MainMenu $NextBootTimeoutSec)) { return }
        $menuText = Get-ScreenText 'menu-off.png'
        Add-Step 'next boot: the main menu has no store row' (-not (Test-Label $menuText @('CUST'))) (($menuText -replace '\s+', ' ').Trim())
        Refresh-LogText
        Add-Step 'next boot: no catalogue request' ((Get-LogMatches 'store_catalogue: fetching') -eq 0) "$((Get-LogMatches 'store_catalogue: fetching')) fetch line(s)"
        Add-Step 'next boot: no served record' ((Get-LogMatches "store_fetch: served '") -eq 0) "$((Get-LogMatches "store_fetch: served '")) served record(s)"
        Add-Step 'next boot: no library index' ((Get-LogMatches 'store_library: indexing') -eq 0) "$((Get-LogMatches 'store_library: indexing')) index line(s)"
        Add-Step 'next boot: no download' ((Get-LogMatches 'store_download: start') -eq 0) "$((Get-LogMatches 'store_download: start')) start line(s)"
        Add-Step 'next boot: no search' ((Get-LogMatches 'store_search: page') -eq 0) "$((Get-LogMatches 'store_search: page')) search page(s)"

        if ((Open-ScreenByRow 'settings_screen' $HelpOptionsRow 'help-options') -lt 0) {
            Add-Step 'next boot: Help & Options opens' $false (Get-Screen)
        } else {
            ClickRow $HelpGeneralRow 'general' | Out-Null
            Wait-ForScreenName 'settings_general_screen' 15 'general' | Out-Null
            ClickRow $GeneralDlcRow 'dlc' | Out-Null
            if (Wait-ForScreenName 'settings_dlc_screen' 15 'dlc page') {
                $page = Wait-ForScreen 'dlc-off.png' 'RHYTHMVERSE' $ScreenTimeoutSec 'dlc page'
                Assert-Label $page @('RHYTHMVERSE', 'OFF') 'next boot: the master reads the committed Off'
            } else {
                Add-Step 'next boot: Downloadable Content opens' $false (Get-Screen)
            }
        }
    } finally {
        Stop-Boot $proc
    }
}

# ------------------------------------------------------------------- report ---

if (-not $NextBootOnly) { Invoke-SessionLeg }
if (-not $SessionOnly) { Invoke-NextBootLeg }

Write-Host ''
Write-Host ("probe_master_restart: {0} step(s), {1} failure(s)" -f $script:steps, $script:failures.Count)
if ($script:failures.Count -gt 0) {
    foreach ($f in $script:failures) { Write-Host "  FAIL $f" }
    Write-Host "  evidence: $base"
    exit 1
}
Write-Host "  evidence: $base"
