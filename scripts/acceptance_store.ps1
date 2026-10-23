# P8 of docs/plans/in-progress/2026-10-13-rhythmverse-store-plan.md: the store's acceptance.
#
# The walk the plan asks for, in the order a player takes it, split into legs so each one can be
# run and read on its own:
#
#   Enable  - a boot on a fresh profile whose settings file never names the master switch (the main
#             menu's refresh row is on): Help & Options > General > Downloadable Content > RhythmVerse draws
#             `RhythmVerse: On`, which is the shipped **default** (RhythmVerse plan §9/P9-1), the
#             boot writes nothing for it, and the main menu already carries the store row. The
#             Control leg is the only one that writes the value off.
#   Store   - the row opens the retail store, the front menu draws RECENTLY ADDED / SEARCH with no
#             count beside either row (P9-5) and the `Customs from RhythmVerse` credit, Recently
#             Added's page carries eligible offers (read from the log, not guessed) and the retail
#             browser opens on them. Then Search runs a real
#             query - the field is seeded with -SearchQuery, so one Done press exercises the
#             request - the answer is drawn, and the same row opens the results page in the retail
#             browser. Then A on the first result opens the details screen, DOWNLOAD is pressed,
#             and the install is asserted on disk, in the receipt and in the log. No price,
#             currency or purchase word appears on the browser, the details page or the browser it
#             returns to.
#   Install - the next boot, same profile, same DLC root: the downloaded song is on the title's
#             own song list, which is the plan's "appears in the song list". The song's name is
#             read out of the Store leg's install line (`store_download: job <job> installed
#             '<Artist - Title>' ...`), so the leg asserts the package it actually installed and
#             not a name this script guessed from the catalogue.
#   Control - a boot with RhythmVerse **Off**: no store activity in the log at all, no store row on
#             the main menu, and the main menu's own **Refresh Songs** row still dispatches
#             its sentinel (`dlc: refresh asked for from the main menu`).
#
# Two scratch fixtures, never the owner's files: a profile directory per leg family (the launcher
# profile and its `settings.toml` sidecar, which is where a committed setting lives) and a DLC root.
# The DLC root starts **empty**, so the store's install is the only custom package in it - which is
# what makes "one package after the download, and the next boot plays it" a measurement rather
# than a reading of somebody's library.
#
# Usage (this machine's execution policy is Restricted):
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\acceptance_store.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\acceptance_store.ps1 -Legs Enable
#   # the rest of the run later, on the fixtures the Enable leg wrote
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\acceptance_store.ps1 -Legs Store,Install -Reuse
#
# Writes <OutDir>/<Label>/ (fixtures, each boot's log copy, report.txt, summary.json) and
# <OutDir>/<Label>-shots/*.png, prints a per-step verdict, and exits non-zero unless every step
# that ran behaved.
param(
    [string]$Label = 'p8-store',
    [string]$BuildDir = 'out/build/win-amd64-release',
    [string]$OutDir = 'out/store-acceptance',
    [string]$GameRoot,
    [string]$UltimateMode = '1',
    # Which legs to run. `Enable` must come first on a fresh evidence directory: it is the boot that
    # proves the store's shipped default (On) on the fixture the later legs then use. It no longer
    # turns the store on - the switch is on unless someone writes it off.
    [ValidateSet('Enable', 'Store', 'Install', 'Control')]
    [string[]]$Legs = @('Enable', 'Store', 'Install', 'Control'),
    # A query the live catalogue answers with eligible offers. The API matches artist OR title,
    # one at a time (plan section 1.5), so this is one word or one name, never both.
    [string]$SearchQuery = 'bush',
    # Row indices. Each is a *hint*: which index carries a row depends on the boot's settings
    # (hidden rows, Play Current Song, the refresh row), so the main-menu rows are found by
    # clicking and checking which screen opened, and the settings pages are asserted by screen.
    [int]$HelpOptionsRow = 1,
    [int]$StoreRow = 4,
    # The merged Help & Options root's own rows are retail's two pages first: How to Play 0,
    # Calibration 1, then our General 2 (measured 2026-10-22 from `settings_screen.cpp`'s root).
    [int]$HelpGeneralRow = 2,
    [int]$GeneralDlcRow = 2,
    [int]$RhythmVerseRow = 0,
    # The main menu's own refresh row, in the fixture's layout (the store row is appended after it).
    [int]$RefreshRow = 3,
    [int]$BootTimeoutSec = 240,
    [int]$ScreenTimeoutSec = 120,
    [int]$CatalogueWaitSec = 120,
    [int]$DownloadWaitSec = 180,
    [int]$InstallBootTimeoutSec = 720,
    [int]$CloseWaitSec = 25,
    # Keep the existing fixtures and evidence directory instead of starting over. That is how the
    # legs are run in separate invocations (Enable now, Store later).
    [switch]$Reuse
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$work = Join-Path $root $BuildDir
$exe = Join-Path $work 'rb_blitz.exe'
if (-not $GameRoot) { $GameRoot = Join-Path $root 'game' }
$base = Join-Path $root (Join-Path $OutDir $Label)
$shots = Join-Path $root (Join-Path $OutDir "$Label-shots")
# `drive_ui.ps1` joins its `-OutDir` against the repository root (measured 2026-10-22: an absolute
# value becomes `<root>\D:\...` and the harness dies with an IOException), so it is handed the
# root-relative spelling; capture/OCR take absolute paths and are given `$shots`.
$shotsRel = $shots
if ($shots.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    $shotsRel = $shots.Substring($root.Length).TrimStart('\', '/')
}
$profileDir = Join-Path $base 'profile'
$controlProfileDir = Join-Path $base 'control-profile'
$dlcRoot = Join-Path $base 'dlc'
$controlDlc = Join-Path $base 'control-dlc'

if (-not (Test-Path -LiteralPath $exe)) { throw "missing $exe" }
if (-not (Test-Path -LiteralPath (Join-Path $GameRoot 'default.xex'))) { throw "no default.xex under $GameRoot" }
if (Get-Process rb_blitz -ErrorAction SilentlyContinue) { throw 'Close the running game first.' }
if ((Test-Path -LiteralPath $base) -and -not $Reuse) {
    throw "Use a fresh -Label or pass -Reuse: $base exists (this harness never overwrites evidence)."
}
New-Item -ItemType Directory -Force -Path $base, $shots | Out-Null

$script:failures = @()
$script:steps = 0
$script:currentLog = ''
$script:logText = ''
$script:installedTitle = ''
$script:installedPath = ''
$script:installedBytes = 0

# ---------------------------------------------------------------- fixtures ---

# Windows PowerShell's `Set-Content -Encoding utf8` writes a BOM, and the profile reader refuses a
# document whose first key carries one (measured 2026-10-20: "no schema_version"). Write bytes.
function Write-TextFile([string]$Path, [string[]]$Lines) {
    $text = ($Lines -join "`r`n") + "`r`n"
    [System.IO.File]::WriteAllText($Path, $text, (New-Object System.Text.UTF8Encoding($false)))
}

function Write-LauncherProfile([string]$Dir) {
    New-Item -ItemType Directory -Force -Path $Dir | Out-Null
    # The launcher profile decides where `settings.toml` lives (beside it), and it must parse: a
    # failed read makes the boot rewrite the settings file before the payload is built, which would
    # drop whatever the Enable leg committed.
    Write-TextFile (Join-Path $Dir 'launcher.toml') @(
        'schema_version = 1',
        '',
        '[launcher]',
        'version = 1',
        'portable = false',
        '',
        '[launch]',
        'target = "ultimate"'
    )
}

# The retail store's replacement row on the main menu is opt-in (both the cvar and the row default
# to off), and the plan's P1 exit asks that it survive the RhythmVerse feature: both profiles ask
# for it, so the Store leg can assert the two rows side by side and the Control leg can press the
# refresh row with the store off.
#
# The two online rows are hidden on purpose: with LEADERBOARDS and ACHIEVEMENTS drawn, the six-row
# menu is full and the store row - which is appended after the refresh row - is the seventh, i.e.
# *selectable but off the visible page* (the P2 defect the plan records). Hiding them is the layout a
# player who turned the offline rows off already has, and it puts every row on one page.
# `$RhythmVerse = $null` leaves the key out of the file entirely, which is how the Enable leg proves
# the shipped default (On, plan §9/P9-1) instead of a value this script wrote: the page must draw On
# from the compiled default, and the boot must not add the key. Only the Control leg passes `$false`.
# `refresh_music_library` is left out for the same reason since P9-12: its default is On again, so
# the fixture proves that default instead of writing it, and the Enable leg checks that the boot
# did not commit it.
function Write-StoreSettings([string]$Dir, [System.Nullable[bool]]$RhythmVerse) {
    $lines = @('schema_version = 1', '', '[general]')
    if ($null -ne $RhythmVerse) {
        if ($RhythmVerse) { $lines += 'rhythmverse = true' } else { $lines += 'rhythmverse = false' }
    }
    $lines += @('', '[interface]', 'hide_main_menu = true',
                "hidden_main_menu = ['splash_leaderboard', 'splash_achievements']")
    Write-TextFile (Join-Path $Dir 'settings.toml') $lines
}

function Initialize-Fixtures([bool]$Fresh) {
    New-Item -ItemType Directory -Force -Path $profileDir, $controlProfileDir, $dlcRoot, $controlDlc | Out-Null
    foreach ($dir in @($profileDir, $controlProfileDir)) {
        if ($Fresh -or -not (Test-Path -LiteralPath (Join-Path $dir 'launcher.toml'))) { Write-LauncherProfile $dir }
    }
    if ($Fresh) {
        # P9-1/P9-12 flipped both switches to On, so the Enable leg proves the defaults rather than
        # flipping them: its profile carries the two hidden online rows the layout needs and never
        # names `rhythmverse` or `refresh_music_library`. The Control leg is the only leg that
        # writes the store value off.
        Write-StoreSettings $profileDir $null
        Write-StoreSettings $controlProfileDir $false
        # A fresh, empty DLC root: this leg must not depend on the owner's own library.
        foreach ($dir in @($dlcRoot, $controlDlc)) {
            Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
        }
    } elseif (-not (Test-Path -LiteralPath (Join-Path $profileDir 'settings.toml'))) {
        Write-StoreSettings $profileDir $null
    }
}

Initialize-Fixtures (-not $Reuse)

# ---------------------------------------------------------------- helpers ---

# Every helper that runs another tracked script goes through a child PowerShell: this machine's
# execution policy is Restricted (docs/guides/build-and-run.md section 0), and `ocr_image.ps1`
# needs Windows PowerShell 5.1 for its WinRT OCR types.
#
# The child's stderr is collected as text, not left as error records: `$ErrorActionPreference =
# "Stop"` turns a native command's stderr into a terminating error, and `capture_window.ps1` says
# "no window yet" on stderr during a scene change - which killed whole probe runs (measured
# 2026-10-22).
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

# A fresh capture every time: reading a file that is already there would OCR the previous run's
# screen (this harness's own evidence directory outlives a single invocation under -Reuse).
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

# The screen the guest is on, from the two instruments that name it: the native mouse's own focus
# line and the help overlay's observer. Both are host-side log lines, so this is a measurement
# rather than an OCR guess.
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

# The host's own row table for the focused list (native_mouse row lines), newest run first. A
# hover re-logs one row, so only a run containing the index counts (the rule
# acceptance_controls.ps1's ClickRow uses).
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
    param([int]$Index, [string]$Tag, [double]$Wait = 3, [int]$RowTimeoutSec = 20)
    $hit = Wait-ForRowTable $Index $RowTimeoutSec
    if (-not $hit) { return $false }
    $x = [math]::Round((($hit[1] + $hit[3]) / 2), 4)
    $y = [math]::Round((($hit[2] + $hit[4]) / 2), 4)
    Write-Host ("  {0}: row {1} at {2},{3}" -f $Tag, $Index, $x, $y)
    Drive @("mouse:$x`:$y", 'wait:1', 'click:left', "wait:$Wait")
    return $true
}

function Test-MainMenu {
    if ((Get-Screen) -eq 'splash_screen' -and $null -ne (Get-RowTable 0)) { return $true }
    $text = Get-ScreenText 'menu.png'
    return ($text.Contains('EXIT GAME') -or $text.Contains('HELP & OPTIONS'))
}

function Return-ToMenu {
    for ($i = 0; $i -lt 4; $i++) {
        if (Test-MainMenu) { return $true }
        Drive @('key:b', 'wait:2')
    }
    return (Test-MainMenu)
}

# Which index carries a main-menu row is a function of the boot's settings, so the measured index
# is a hint and the screen the click opens is the oracle: try the hint, then the rest of the page,
# backing out of whatever opened in between.
function Open-ScreenByRow {
    param([string]$Expected, [int]$Hint, [string]$Tag)
    if (-not (Return-ToMenu)) {
        Write-Host "  !! ${Tag}: the main menu could not be reached before the row scan"
        return -1
    }
    for ($attempt = 0; $attempt -lt 8; $attempt++) {
        $index = ($Hint + $attempt) % 8
        if (-not (Get-RowTable $index)) { continue }
        if (-not (ClickRow $index "$Tag-row$index" -RowTimeoutSec 5)) { continue }
        if (Wait-ForScreenName $Expected 12 "$Tag row $index") { return $index }
        Return-ToMenu | Out-Null
    }
    return -1
}

# -------------------------------------------------------------- assertions ---

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

function Assert-Contains([string]$Text, [string]$Needle, [string]$Name) {
    $normalized = $Text.ToUpperInvariant() -replace '[^A-Z0-9]', ''
    Add-Step $Name $normalized.Contains(($Needle.ToUpperInvariant() -replace '[^A-Z0-9]', '')) (($Text -replace '\s+', ' ').Trim())
}

function Assert-Lacks([string]$Text, [string]$Needle, [string]$Name) {
    $normalized = $Text.ToUpperInvariant() -replace '[^A-Z0-9]', ''
    Add-Step $Name (-not $normalized.Contains(($Needle.ToUpperInvariant() -replace '[^A-Z0-9]', ''))) (($Text -replace '\s+', ' ').Trim())
}

# A long row label's OCR drops letters often enough that a strict substring is a false negative:
# measured 2026-10-22, `REFRESH MUSIC LIBRARY` came back as `REF ESH MUSIC LIBR RY` (no R, no A) -
# the row is `REFRESH SONGS` since that day, and it gets the same fragment treatment.
# A label check therefore asks for its distinctive word(s) with the whitespace folded out, and the
# row's *behaviour* is asserted from the log - never from the label alone.
function Assert-Label([string]$Text, [string[]]$Words, [string]$Name) {
    $normalized = $Text.ToUpperInvariant() -replace '[^A-Z0-9]', ''
    $missing = @()
    foreach ($word in $Words) {
        $needle = [regex]::Replace($word.ToUpperInvariant(), '[^A-Z0-9]', '')
        if (-not $normalized.Contains($needle)) { $missing += $needle }
    }
    Add-Step $Name ($missing.Count -eq 0) "missing [$($missing -join ', ')] of '$(($Words -join ' '))'"
}

function Get-LogMatches([string]$Pattern) {
    return ([regex]::Matches($script:logText, $Pattern)).Count
}

# A boolean form of the same fold, for a decision the route has to make on the screen it is looking
# at (the label check itself stays `Assert-Label`, which reports what was missing).
function Test-Label([string]$Text, [string[]]$Words) {
    $normalized = $Text.ToUpperInvariant() -replace '[^A-Z0-9]', ''
    foreach ($word in $Words) {
        if (-not $normalized.Contains([regex]::Replace($word.ToUpperInvariant(), '[^A-Z0-9]', ''))) {
            return $false
        }
    }
    return $true
}

function Assert-Log([string]$Pattern, [string]$Name, [int]$AtLeast = 1) {
    Add-Step $Name ((Get-LogMatches $Pattern) -ge $AtLeast) "$((Get-LogMatches $Pattern)) match(es) of /$Pattern/"
}

function Assert-LogAbsent([string]$Pattern, [string]$Name) {
    Add-Step $Name ((Get-LogMatches $Pattern) -eq 0) "$((Get-LogMatches $Pattern)) match(es) of /$Pattern/"
}

function Refresh-LogText {
    $script:logText = Get-FileText $script:currentLog
}

# A live lookup: the assertions read the snapshot, so a *wait* has to refresh it or it waits on a
# boot that has already moved on.
function Get-LogHas([string]$Pattern) {
    Refresh-LogText
    return (Get-LogMatches $Pattern) -gt 0
}

# The main menu is up. Two host-side oracles, either one enough: the content pipeline's own
# `usable menu` line (a boot that ran the discovery pass), or the native mouse capture's row table -
# the title and the connect panel both draw no list, so a row line means a menu whatever the
# pipeline logged (measured 2026-10-22: a boot that skipped the pass, i.e. no DLC change since the
# last one, never logs `usable menu`).
function Test-MenuUp {
    if ((Get-Screen) -ne 'splash_screen') { return $false }
    if (Get-LogHas 'content_pipeline: usable menu') { return $true }
    return ($null -ne (Get-RowTable 0))
}

# ---------------------------------------------------------------- process ---

function Start-Leg([string]$Leg, [string]$Profile, [string]$Dlc, [int]$BootTimeout, [string]$SearchSeed = '') {
    $logPath = Join-Path $base "$Leg.log"
    if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath -Force }
    $script:currentLog = $logPath
    $script:logText = ''
    $arguments = @(
        "--game_data_root=""$GameRoot""",
        "--ultimate_mode=$UltimateMode",
        '--mnk_mode=1',
        "--user_data_root=""$Profile""",
        "--launcher_profile=""$(Join-Path $Profile 'launcher.toml')""",
        "--dlc_root=""$Dlc""",
        "--log_file=""$logPath""",
        # Info, not debug: the debug stream is APU/GPU chatter (a stalled boot wrote 4.7 MB in four
        # minutes), and every line this harness asserts on is an info line.
        '--log_level=info',
        '--log_flush_interval=1',
        '--log_max_file_size_mb=200',
        '--mouse_native_trace=true'
    )
    # The authored key pages type one character per press, which is a long route for a script; the
    # field is seeded so one Done press exercises the request, the answer and the strip. Only the
    # Store leg seeds it - the other legs never open the store.
    if ($SearchSeed -ne '') { $arguments += "--store_search_seed=$SearchSeed" }
    Write-Host ''
    Write-Host "=== $Leg (profile=$Profile dlc=$Dlc) ==="
    Write-Host "  args: $($arguments -join ' ')"
    $proc = Start-Process -FilePath $exe -WorkingDirectory $work -PassThru -ArgumentList $arguments
    $proc.Id | Set-Content (Join-Path $base "$Leg.pid")

    $deadline = (Get-Date).AddSeconds($BootTimeout)
    $windowed = $false
    while ((Get-Date) -lt $deadline) {
        try {
            $proc.Refresh()
            if ($proc.HasExited) { break }
            if ($proc.MainWindowHandle -ne [IntPtr]::Zero) { $windowed = $true; break }
        } catch { }
        Start-Sleep -Seconds 2
    }
    if (-not $windowed) { throw "$Leg`: no game window after ${BootTimeout}s" }
    return $proc
}

function Stop-Leg($Proc) {
    try {
        if (-not $Proc.HasExited) {
            $Proc.CloseMainWindow() | Out-Null
            if (-not $Proc.WaitForExit($CloseWaitSec * 1000)) { Stop-Process -Id $Proc.Id -Force }
        }
    } catch { }
    Start-Sleep -Seconds 2
    Refresh-LogText
}

# Boot -> title -> main menu, and the boot half of it is **log-driven on purpose**. The payload is
# installed ~50 s in and the title draws after that, so the route starts from one measured title
# rather than a sleep; after the accept the only instrument is the host's own log, because a capture
# started while the title's discovery pass is running is what wedged five boots of 2026-10-22 (the
# pass left 100-450 ms after the enumeration in every boot nothing else touched, and sat for minutes
# with the guest idle in every boot captured and OCR'd every ~18 s). `splash_screen` is the screen
# *name* at the title as well, so the screen name alone is not enough - the menu is read as
# `content_pipeline: usable menu` plus that screen name, both host lines. The labels are read once
# afterwards, when the menu is up and nothing is racing it.
function Enter-MainMenu([int]$BootTimeout) {
    $title = Wait-ForScreen 'title.png' 'TO START' $BootTimeout 'title'
    Add-Step 'boot: the title screen is drawn' ($title.Contains('TO START'))
    # The route is the probe's own: press, wait, look - all of it from the host log, so that no
    # capture ever runs while the discovery pass is in flight. "Look" is a hover at the list, which
    # costs two seconds and makes the native mouse capture log the row table; the menu is the only
    # screen in this build that has one (the title and the connect panel draw no list). A press is
    # sent only after a look that found no rows, and one more look follows every press before the
    # next one - without a button, the retried accept that opened the menu in every run of
    # 2026-10-22 could land on a row (measured: the first accept alone was swallowed, the second one
    # opened the menu).
    $presses = 0
    # The leg's own budget, not a fixed 300 s: the boot that follows a library change runs the
    # title's discovery pass first (measured 2026-10-22: 1 package, ~170 ms per batch, then the
    # title owns the panel), and the Install leg is started with `-InstallBootTimeoutSec`.
    $deadline = (Get-Date).AddSeconds([Math]::Max(300, $BootTimeout))
    while ((Get-Date) -lt $deadline) {
        Drive @('mouse:0.5:0.78', 'wait:1')
        if (Test-MenuUp) { break }
        if ($presses -ge 40) { break }
        $presses += 1
        Drive @('key:a', 'wait:12')
    }
    $menuReady = Test-MenuUp
    if (-not $menuReady) {
        Add-Step 'boot: the main menu is up' $false "$(Get-Screen) after $presses accept(s); no menu oracle"
        return $false
    }
    # A hover makes the native mouse capture the focused list's geometry, which is the table the row
    # clicks are built from; the labels are read once afterwards, as the step's own evidence.
    Drive @('mouse:0.5:0.78', 'wait:2')
    $rows = Wait-ForRowTable 0 60
    $text = Get-ScreenText 'menu.png'
    Add-Step 'boot: the main menu is up' ($null -ne $rows) (($text -replace '\s+', ' ').Trim())
    return ($null -ne $rows)
}

function Invoke-EnableLeg {
    $proc = Start-Leg 'Enable' $profileDir $dlcRoot $BootTimeoutSec
    try {
        if (-not (Enter-MainMenu $BootTimeoutSec)) { return }
        # The store is On by default (P9-1), so a profile that never named the setting already
        # carries the row - the boot-level half of the default. The row reads `RhythmVerse` since
        # P9-12; OCR loses letters from a highlighted row, so the check asks for a fragment
        # (`HYTH` is what every measured reading of the shortened label keeps). The refresh row is
        # the other half of P9-12: its own default is On again, so it is on the menu without the
        # fixture naming `refresh_music_library`.
        $menuText = Get-ScreenText 'menu.png'
        Assert-Label $menuText @('HYTH') 'enable: the main menu carries the store row on a default profile'
        Assert-Label $menuText @('REFRESH') 'enable: the main menu carries the refresh row on a default profile'

        $index = Open-ScreenByRow 'settings_screen' $HelpOptionsRow 'help-options'
        Add-Step 'enable: Help & Options opens' ($index -ge 0) "row $index"
        if ($index -lt 0) { return }

        if (-not (ClickRow $HelpGeneralRow 'general' -Wait 2) -or
            -not (Wait-ForScreenName 'settings_general_screen' 15 'general')) {
            Add-Step 'enable: General opens' $false (Get-Screen)
            return
        }
        Add-Step 'enable: General opens' $true "row $HelpGeneralRow"
        if (-not (ClickRow $GeneralDlcRow 'downloadable-content' -Wait 2) -or
            -not (Wait-ForScreenName 'settings_dlc_screen' 15 'dlc page')) {
            Add-Step 'enable: Downloadable Content opens' $false (Get-Screen)
            return
        }
        Add-Step 'enable: Downloadable Content opens' $true "row $GeneralDlcRow"

        $dlcText = Get-ScreenText 'dlc-rows.png'
        # The four settings rows and the store submenu, in P9-3's order. `SONGS` is the Refresh Songs
        # row (P9-12) - its `On` state is pinned by `rb_blitz_settings_screen_tests` and by the
        # no-commit check below, because OCR may drop the value off a long line.
        Assert-Label $dlcText @('RHYTHMVERSE', 'CACHE DLC', 'REFRESH', 'SONGS', 'DLC SCAN', 'DLC MENU') 'enable: DLC page has the five settings rows'
        if (-not (ClickRow $RhythmVerseRow 'rhythmverse' -Wait 2) -or
            -not (Wait-ForScreenName 'settings_rhythmverse_screen' 15 'RhythmVerse page')) {
            Add-Step 'enable: RhythmVerse submenu opens' $false (Get-Screen)
            return
        }
        Add-Step 'enable: RhythmVerse submenu opens' $true "row $RhythmVerseRow"
        $rvText = Get-ScreenText 'rhythmverse-rows.png'
        Assert-Label $rvText @('SEARCH SONG FILTER') 'enable: the renamed filter is reachable'

        $before = Wait-ForScreen 'dlc-before.png' 'RHYTHMVERSE' $ScreenTimeoutSec 'dlc page rows'
        Add-Step 'enable: the master switch reads On with no committed value' ($before -match 'RHYTHMVERSE\s*:?\s*ON') (($before -replace '\s+', ' ').Trim())
        Copy-Item -LiteralPath (Join-Path $profileDir 'settings.toml') -Destination (Join-Path $base 'profile-after-enable.toml') -Force

        # A read must not become a write: the fixture's file never named the master, so an absent
        # key after the boot is what makes the On above the *compiled default* rather than a value
        # this boot committed. The Store leg is the behavioural half - the same profile, the switch
        # untouched, and the store serves. The refresh entry's own default is measured the same
        # way (P9-12): the row is on the menu above, and the boot did not write its key.
        $settingsText = Get-FileText (Join-Path $profileDir 'settings.toml')
        Add-Step 'enable: the boot did not commit the default into the profile' ($settingsText -notmatch '(?m)^\s*rhythmverse\s*=') ($settingsText -replace "`r?`n", ' | ')
        Add-Step 'enable: the boot did not commit the refresh default' ($settingsText -notmatch '(?m)^\s*refresh_music_library\s*=') ($settingsText -replace "`r?`n", ' | ')

        Drive @('key:b', 'wait:2')
        Add-Step 'enable: RhythmVerse Back returns to DLC' (Wait-ForScreenName 'settings_dlc_screen' 15 'back to DLC') (Get-Screen)
        # Leave the page before the process ends, so the boot's own shutdown path runs.
        Drive @('key:b', 'wait:2', 'key:b', 'wait:2', 'key:b', 'wait:2')
        Save-Shot 'enable-back.png' | Out-Null
    } finally {
        Stop-Leg $proc
    }
}

function Invoke-StoreLeg {
    $proc = Start-Leg 'Store' $profileDir $dlcRoot $BootTimeoutSec $SearchQuery
    try {
        if (-not (Enter-MainMenu $BootTimeoutSec)) { return }
        $menuText = Get-ScreenText 'menu.png'
        # The row is `RhythmVerse` (renamed from `RhythmVerse Customs` on 2026-10-22), but OCR
        # reads it several ways depending on the highlight: `HYTH VERSE CUSTOM` and
        # `RHYTHMVERSE CUSTOM` were both measured on 2026-10-22, and the shorter label leaves fewer
        # letters to drop. `HYTH` is the part the measured readings keep; the row's *behaviour*
        # below is the real oracle.
        Assert-Label $menuText @('HYTH') 'store: the main menu carries the store row'
        Assert-Label $menuText @('REFRESH') 'store: the refresh row is still on the main menu'

        $index = Open-ScreenByRow 'store_menu_screen' $StoreRow 'store-entry'
        Add-Step 'store: the RhythmVerse row opens the retail store' ($index -ge 0) "row $index"
        if ($index -lt 0) { return }

        $storeText = Wait-ForScreen 'store.png' 'RECENTLY' $ScreenTimeoutSec 'the store front menu'
        # P9-5 renamed the row from `Recent` to `Recently Added` and removed the `1` the front-menu
        # list drew beside both rows (the served row carries retail's own `random_song` no-count
        # marker; `store_front_menu_row` in docs/engine/diagnostics.md). `Assert-Lacks` folds
        # whitespace out, so the check is exactly the digit the old row drew next to its label.
        Assert-Contains $storeText 'RECENTLY' 'store: the front menu draws Recently Added'
        Assert-Contains $storeText 'SEARCH' 'store: the front menu draws Search'
        Assert-Lacks $storeText 'RECENTLYADDED1' 'store: no count is drawn beside Recently Added'
        Assert-Lacks $storeText 'SEARCH1' 'store: no count is drawn beside Search'
        # The header draws the locale entry `store` with RhythmVerse's own name on it (P3's
        # `SetStoreHeading`) and the OCR reads the whole word; it is the *menu row's* highlight that
        # eats letters (`HYTH VERSE CUSTOM`), which is why only that check asks for a fragment
        # (measured 2026-10-22).
        Assert-Label $storeText @('RHYTHMVERSE') 'store: the heading names RhythmVerse'
        # P7's credit line is outranked by the loading sentence while the catalogue holds no offer
        # yet, and it is held for 12 s after the front menu is served - so it is read once the first
        # page has landed, not on the entry capture (measured 2026-10-22: the entry capture reads
        # `Loading the RhythmVerse catalogue...`). The OCR fragments the line, hence the tail.
        $deadline = (Get-Date).AddSeconds($CatalogueWaitSec)
        while ((Get-Date) -lt $deadline) {
            Refresh-LogText
            if ($script:logText -match 'store_catalogue: page \d+ -> ') { break }
            Start-Sleep -Seconds 1
        }
        $creditText = Get-ScreenText 'store-credit.png'
        Assert-Label $creditText @('USTOMS') 'store: the credit strip reads "Customs from RhythmVerse"'

        # The catalogue starts when the store opens; a page pressed before it is there is a working
        # empty page that returns to the menu, so the walk waits for the offers first.
        $deadline = (Get-Date).AddSeconds($CatalogueWaitSec)
        while ((Get-Date) -lt $deadline) {
            Refresh-LogText
            if ($script:logText -match 'store_catalogue: \d+ page\(s\) -> (\d+) offer\(s\) for the Recent list') { break }
            Start-Sleep -Seconds 3
        }
        Refresh-LogText
        $recent = [regex]::Match($script:logText, 'store_catalogue: \d+ page\(s\) -> (\d+) offer\(s\) for the Recent list')
        $recentCount = if ($recent.Success) { [int]$recent.Groups[1].Value } else { 0 }
        Add-Step 'store: the catalogue answered eligible offers' ($recentCount -ge 1) "$recentCount offer(s)"

        # Recently Added is the front menu's first row.
        Drive @('key:lstick_up', 'wait:1')
        # `store_fetch: served '<path>' ...` is the shipping line (`LogOnce`, info). The per-page
        # `the served page carries N offer(s), M of them marked Downloaded` line is **trace-only**
        # (`store_fetch.cpp`), so a boot without `--store_trace` never writes it - which is what this
        # leg booted on 2026-10-22 and waited 300 s for.
        $servedBefore = Get-LogMatches "store_fetch: served '"
        Drive @('key:a', 'wait:4')
        $deadline = (Get-Date).AddSeconds($ScreenTimeoutSec)
        while ((Get-Date) -lt $deadline) {
            Refresh-LogText
            if ((Get-LogMatches "store_fetch: served '") -gt $servedBefore) { break }
            Start-Sleep -Seconds 2
        }
        Start-Sleep -Seconds 3
        $browserText = Get-ScreenText 'browser.png'
        Assert-Log "store_fetch: served 'dlc_store/" 'store: the page was served to the retail parser'
        Add-Step 'store: the retail browser is the current screen' (Wait-ForScreenName 'store_browser_screen' 30 'Recently Added') (Get-Screen)
        Assert-Lacks $browserText 'MICROSOFT' 'store: no Microsoft Points chrome on the browser'
        Assert-Lacks $browserText 'BUY NOW' 'store: no Buy Now row on the browser'

        # Search: Back to the menu, the second row (Search) opens the authored keys, Done submits
        # the seeded query, the answer is drawn, and the same row then opens the results page.
        Drive @('key:b', 'wait:3')
        Add-Step 'store: Back returns to the store menu' (Wait-ForScreenName 'store_menu_screen' 20 'back from Recently Added')
        Drive @('key:lstick_up', 'wait:1', 'key:lstick_down', 'wait:1', 'key:a', 'wait:4')
        $keysText = Wait-ForScreen 'keys.png' 'LETTERS' $ScreenTimeoutSec 'the search keys'
        Assert-Contains $keysText 'LETTERS' 'store: the Search row opens the authored keys'
        $pagesBefore = Get-LogMatches "store_fetch: served '"

        Drive @('key:a', 'wait:2')
        $deadline = (Get-Date).AddSeconds($ScreenTimeoutSec)
        while ((Get-Date) -lt $deadline) {
            Refresh-LogText
            if ($script:logText -match 'store_search: page \d+ -> \d+ record\(s\) answered, \d+ eligible offer\(s\)') { break }
            Start-Sleep -Seconds 2
        }
        Refresh-LogText
        $answer = [regex]::Match($script:logText, 'store_search: page \d+ -> (\d+) record\(s\) answered, (\d+) eligible offer\(s\)')
        $eligible = if ($answer.Success) { [int]$answer.Groups[2].Value } else { 0 }
        $records = if ($answer.Success) { $answer.Groups[1].Value } else { '0' }
        Add-Step 'store: the search answered eligible offers' ($eligible -ge 1) "$eligible eligible of $records record(s)"
        Start-Sleep -Seconds 3
        $answerText = Get-ScreenText 'answer.png'
        Assert-Contains $answerText 'MATCHES FOR' 'store: the strip reports the matches'

        Drive @('key:b', 'wait:3')
        Add-Step 'store: Back returns to the store menu from the keys' (Wait-ForScreenName 'store_menu_screen' 20 'back from the keys')
        Drive @('key:lstick_up', 'wait:1', 'key:lstick_down', 'wait:1', 'key:a', 'wait:4')
        $deadline = (Get-Date).AddSeconds($ScreenTimeoutSec)
        while ((Get-Date) -lt $deadline) {
            Refresh-LogText
            if ((Get-LogMatches "store_fetch: served '") -gt $pagesBefore) { break }
            Start-Sleep -Seconds 2
        }
        Start-Sleep -Seconds 3
        $resultsText = Get-ScreenText 'results.png'
        Add-Step 'store: the results page opens the retail browser' (Wait-ForScreenName 'store_browser_screen' 30 'results') (Get-Screen)
        Assert-Lacks $resultsText 'MICROSOFT' 'store: no Microsoft Points chrome on the results list'

        # A on the first result: the details screen, its Download row and no purchase chrome.
        Drive @('key:a', 'wait:4')
        Add-Step 'store: A opens the details screen' (Wait-ForScreenName 'store_info_screen' 20 'details')
        Start-Sleep -Seconds 3
        $detailsText = Get-ScreenText 'details.png'
        Assert-Contains $detailsText 'DOWNLOAD' 'store: the details page offers Download'
        Assert-Lacks $detailsText 'BUY NOW' 'store: the details page offers no Buy Now'
        Assert-Lacks $detailsText 'MICROSOFT' 'store: the details page carries no currency chrome'
        Assert-Lacks $detailsText 'POINTS' 'store: the details page carries no points chrome'

        # Press Download (row 0 is where this build's Download row sits) and follow the transfer.
        $installsBefore = Get-LogMatches "store_download: job \w+ installed '"
        Drive @('key:a', 'wait:2')
        $deadline = (Get-Date).AddSeconds($DownloadWaitSec)
        $installed = $null
        while ((Get-Date) -lt $deadline) {
            Refresh-LogText
            if ((Get-LogMatches "store_download: job \w+ installed '") -gt $installsBefore) {
                $installed = [regex]::Match($script:logText, "store_download: job (\w+) installed '([^']*)' (.+?) \((\d+) bytes, title (\d+)\)")
                if ($installed.Success) { break }
            }
            Start-Sleep -Seconds 2
        }
        $stripText = Get-ScreenText 'strip.png'
        Assert-Log 'store_download: start accepted for \w+' 'store: the Download press reached the service'
        Add-Step 'store: the transfer installed a package' ($null -ne $installed -and $installed.Success) $(if ($installed -and $installed.Success) { $installed.Value } else { 'no install line' })
        if ($installed -and $installed.Success) {
            $script:installedTitle = $installed.Groups[2].Value
            $script:installedPath = $installed.Groups[3].Value
            $script:installedBytes = [int]$installed.Groups[4].Value
            Assert-Contains $stripText 'INSTALLED' 'store: the strip reports the install'
        }

        # The install is a file in the resolved root, the staging directory is empty again, the
        # receipt carries the transfer, and the host asked for its refresh.
        if ($script:installedPath) {
            $full = if ([System.IO.Path]::IsPathRooted($script:installedPath)) { $script:installedPath } else { Join-Path $work $script:installedPath }
            Add-Step 'store: the installed file exists' (Test-Path -LiteralPath $full) $full
            if (Test-Path -LiteralPath $full) {
                $size = (Get-Item -LiteralPath $full).Length
                Add-Step 'store: the installed file has the transferred length' ($size -eq $script:installedBytes) "$size byte(s)"
            }
            $containers = @(Get-ChildItem -LiteralPath $dlcRoot -Filter '*.con' -File -ErrorAction SilentlyContinue)
            Add-Step 'store: the DLC root holds exactly one container' ($containers.Count -eq 1) "$($containers.Count) container(s)"
            $titleLevels = @(Get-ChildItem -LiteralPath $dlcRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^[0-9A-Fa-f]{8}$' })
            Add-Step 'store: no title-id directory was created in the flat root' ($titleLevels.Count -eq 0) "$($titleLevels.Count) title-id level(s)"
            $staging = Join-Path $dlcRoot '.rbbz-store-staging'
            $leftovers = if (Test-Path -LiteralPath $staging) { @(Get-ChildItem -LiteralPath $staging -Recurse -File -ErrorAction SilentlyContinue) } else { @() }
            Add-Step 'store: staging is empty after the commit' ($leftovers.Count -eq 0) "$($leftovers.Count) leftover(s)"
        }
        $receiptText = Get-FileText (Join-Path $profileDir 'store\downloads.tsv')
        Add-Step 'store: the receipt records the transfer as pending' ($receiptText -match '(?m)^[^\s#].*\spending\s') (($receiptText -replace "`r?`n", ' | '))
        Assert-Log "dlc: the store's install asked for a refresh" 'store: the install asked for a library refresh'
        Assert-Log 'dlc: the refresh finished \(\d+ package\(s\)\)' 'store: the refresh finished'

        # Back to the browser: the return state carries no price chrome either.
        Drive @('key:b', 'wait:3')
        Start-Sleep -Seconds 2
        $returnText = Get-ScreenText 'browser-return.png'
        Assert-Lacks $returnText 'MICROSOFT' 'store: no points chrome when returning to the browser'
    } finally {
        Stop-Leg $proc
    }
    # The log is the fallback when the Store leg ran in an earlier invocation.
    if (-not $script:installedTitle) {
        $script:logText = Get-FileText (Join-Path $base 'Store.log')
        $m = [regex]::Match($script:logText, "store_download: job (\w+) installed '([^']*)' (.+?) \((\d+) bytes, title (\d+)\)")
        if ($m.Success) {
            $script:installedTitle = $m.Groups[2].Value
            $script:installedPath = $m.Groups[3].Value
            $script:installedBytes = [int]$m.Groups[4].Value
        }
    }
}

function Invoke-InstallLeg {
    if (-not $script:installedTitle) {
        # The Store leg may have run in an earlier invocation (`-Legs Install -Reuse`), so the
        # install line is read out of `Store.log` here rather than relying on the Store leg's own
        # variables: it is the only place the *title* survives (the receipt keeps none by design).
        $storeLog = Get-FileText (Join-Path $base 'Store.log')
        $m = [regex]::Match($storeLog, "store_download: job (\w+) installed '([^']*)' (.+?) \((\d+) bytes, title (\d+)\)")
        if ($m.Success) {
            $script:installedTitle = $m.Groups[2].Value
            $script:installedPath = $m.Groups[3].Value
            $script:installedBytes = [int]$m.Groups[4].Value
        }
    }
    if (-not $script:installedTitle) {
        Add-Step 'install: a downloaded title is known from the Store leg' $false 'no install line in Store.log'
        return
    }
    # The plan's own rule: the next boot's discovery is what makes the song playable, which is why
    # the strip says "Restart the game to play it".
    $proc = Start-Leg 'Install' $profileDir $dlcRoot $InstallBootTimeoutSec
    try {
        if (-not (Enter-MainMenu $InstallBootTimeoutSec)) { return }
        # The press is retried, and a keyboard accept follows the click: the generated payload lands
        # around the first click (measured 2026-10-22: `menu_filter: patched a file ...` in the same
        # millisecond as the click, with the panel reporting `available=false`), and a click sent
        # into that window is dropped - the screen stayed on the main menu for six minutes.
        $opened = $false
        $listText = ''
        for ($attempt = 0; $attempt -lt 4 -and -not $opened; $attempt++) {
            ClickRow 0 'play' | Out-Null
            $listText = Wait-ForScreen 'songs.png' 'YOUR SONGS' 25 'the song list'
            if ($listText.Contains('YOUR SONGS')) { $opened = $true; break }
            # Row 0 is drawn selected, so the accept key is the same press without the mouse.
            Drive @('key:a', 'wait:5')
            $listText = Get-ScreenText 'songs.png'
            if ($listText.Contains('YOUR SONGS')) { $opened = $true; break }
            Drive @('mouse:0.5:0.78', 'wait:2')
        }
        Add-Step 'install: Play opens the song list' $opened (Get-Screen)
        if (-not $opened) { return }
        Assert-Contains $listText 'YOUR SONGS' 'install: the song list is drawn'
        Refresh-LogText
        # The discovery line, not the Downloaded matcher's: `store_library: the loaded DLC library
        # declares N song(s)` belongs to the Store leg (the index is built when the store asks for
        # a page - measured 2026-10-22, this boot never opens the store, so the line cannot exist).
        # What this leg proves is that the title *presented* the installed package and listed it.
        Assert-Log 'dlc: \d+ saved-game library package\(s\) presented as content type' 'install: the installed package is presented to the title'

        # The row is drawn as "Artist - Title"; a row too long for the column is truncated, so
        # every word of the title that is long enough to be a word is required, not the whole
        # string. The list is small on purpose (a scratch root holding the one download), so the
        # row is on the first page.
        $title = $script:installedTitle
        $at = $title.IndexOf(' - ')
        if ($at -ge 0) { $title = $title.Substring($at + 3) }
        $words = @($title.ToUpperInvariant() -split '[^A-Z0-9]+' | Where-Object { $_.Length -ge 4 })
        if ($words.Count -eq 0) { $words = @($title.ToUpperInvariant()) }
        $missing = @($words | Where-Object { -not $listText.Contains($_) })
        Add-Step 'install: the downloaded song is on the song list' ($missing.Count -eq 0) "title '$title'; missing [$($missing -join ', ')]"
    } finally {
        Stop-Leg $proc
    }
}

function Invoke-ControlLeg {
    $proc = Start-Leg 'Control' $controlProfileDir $controlDlc $BootTimeoutSec
    try {
        if (-not (Enter-MainMenu $BootTimeoutSec)) { return }
        $menuText = Get-ScreenText 'menu.png'
        # Two spellings, because the row's OCR changes with the highlight: selected, the label
        # loses its middle letters (`HYTH VERSE CUSTOM`); unselected it read `RHYTHMVERSE CUSTOMS`
        # (the fixture menus above were read that way on 2026-10-22; the label itself is
        # `RhythmVerse` since that day). One negative check on either spelling can miss, so both
        # are asserted.
        Assert-Lacks $menuText 'HYTH VERSE' 'control: the main menu has no store row while the feature is Off'
        Assert-Lacks $menuText 'RHYTHMVERSE' 'control: the main menu draws no RhythmVerse label'
        Assert-Label $menuText @('REFRESH') 'control: the refresh row is on the main menu'

        # Press the refresh row and follow its sentinel. The fixture's layout puts it at
        # `-RefreshRow`; the hint is tried first (a click that opens another screen is backed out
        # of) and only then does the scan walk the page, because several main-menu rows end in
        # screens that take a while to leave.
        $found = -1
        if (Return-ToMenu) {
            if ((Get-RowTable $RefreshRow) -and (ClickRow $RefreshRow 'control-refresh-row' -Wait 3)) {
                Start-Sleep -Seconds 3
                Refresh-LogText
                if ($script:logText -match 'dlc: refresh asked for from the main menu') { $found = $RefreshRow }
            }
        }
        for ($attempt = 0; $attempt -lt 8 -and $found -lt 0; $attempt++) {
            if (-not (Return-ToMenu)) { break }
            if (-not (Get-RowTable $attempt)) { continue }
            ClickRow $attempt "control-refresh-row$attempt" | Out-Null
            Start-Sleep -Seconds 3
            Refresh-LogText
            if ($script:logText -match 'dlc: refresh asked for from the main menu') { $found = $attempt }
        }
        Add-Step 'control: the Refresh Songs row dispatches its refresh' ($found -ge 0) "row $found"

        Refresh-LogText
        # The modules are installed on every boot (their own lines are here), so an absent request
        # is the switch doing its job rather than nothing being loaded.
        Assert-Log 'store_search: transport ready' 'control: the search transport was installed'
        Assert-LogAbsent 'store_catalogue: fetching' 'control: the catalogue never fetched'
        Assert-LogAbsent "store_fetch: served '" 'control: no store record was served'
        Assert-LogAbsent 'store_fetch: the served page carries' 'control: no store page was served'
        # The shipping line is the one a non-trace boot writes; both are checked so a control boot
        # cannot pass on a missing trace line alone.
        Assert-LogAbsent "store_fetch: served '" 'control: no store record was served'
        Assert-LogAbsent 'store_library: indexing' 'control: the Downloaded index was not built'
        Assert-LogAbsent 'store_download: start' 'control: no download was started'
        Assert-LogAbsent 'store_search: page ' 'control: no search page was requested'
        $storeLines = @(($script:logText -split "`r?`n") | Where-Object { $_ -match 'store_[a-z]+:' })
        Add-Step 'control: the boot carries only the store modules itself' ($storeLines.Count -le 8) "$($storeLines.Count) store line(s)"
        $storeLines | ForEach-Object { Write-Host "    $_" }
    } finally {
        Stop-Leg $proc
    }
}

# ------------------------------------------------------------------- main ---

foreach ($leg in $Legs) {
    try {
        switch ($leg) {
            'Enable' { Invoke-EnableLeg }
            'Store' { Invoke-StoreLeg }
            'Install' { Invoke-InstallLeg }
            'Control' { Invoke-ControlLeg }
        }
    } catch {
        Add-Step "$leg leg" $false $_.Exception.Message
    }
}

$summary = [ordered]@{
    label          = $Label
    legs           = $Legs
    steps          = $script:steps
    failures       = $script:failures.Count
    failureList    = $script:failures
    installedTitle = $script:installedTitle
    installedPath  = $script:installedPath
    installedBytes = $script:installedBytes
    profile        = $profileDir
    dlcRoot        = $dlcRoot
    searchQuery    = $SearchQuery
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $base 'summary.json')

$report = @()
$report += "acceptance_store $Label"
$report += "legs     : $($Legs -join ', ')"
$report += "profile  : $profileDir"
$report += "dlc root : $dlcRoot"
$report += "installed: $($script:installedTitle) [$($script:installedPath)]"
$report += ''
$report += "steps    : $($script:steps)"
$report += "failures : $($script:failures.Count)"
foreach ($f in $script:failures) { $report += "  FAIL $f" }
$report | Set-Content -LiteralPath (Join-Path $base 'report.txt') -Encoding utf8
$report | Write-Host

$ok = $script:failures.Count -eq 0 -and $script:steps -gt 0
Write-Host ''
Write-Host ("store acceptance: {0} ({1} step(s), {2} failure(s))" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $script:steps, $script:failures.Count)
if (-not $ok) { exit 1 }
exit 0
