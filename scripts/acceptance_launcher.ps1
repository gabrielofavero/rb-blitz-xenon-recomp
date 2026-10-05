# Launcher acceptance: the installed launcher, end to end.
#
# E3 of docs/plans/launcher-plan.md. Everything else about the launcher is checked against
# the build tree - the unit tests in a host process, the headless reports in the build
# folder, the captures in scripts/capture_launcher.ps1. This script checks the thing a user
# actually gets: a silent install into a scratch folder, the launcher that install put
# there, the profile written beside it, the command line it prints, and the game that boots
# from it with the licence, the Ultimate payload, the DLC and the save folder the profile
# asked for.
#
# One install, used four times, so each leg differs from the others by one thing:
#
#   retail    before the mod is installed: the vanilla route, with the payload's folder
#             absent from disk and the profile asking for the retail game
#   ultimate  after rb_blitz_setup_helper.exe install-ultimate (B8's own command, folder
#             source, no download): the payload mounts and the guest runs out of the overlay
#   demo      the XBLA trial: --license_mask=0 on the command line and the trial licence in
#             the boot log
#   save      a --user_data_root override, isolated under out/ exactly as
#             scripts/acceptance_persistence.ps1 isolates one, so the machine's own
#             Documents\rb_blitz is not what is being checked
#
# Every leg writes its own profile, so "the launcher's settings reach the game" is checked
# rather than assumed. How they travel is worth stating, because it is argv and not a config
# file: the launcher passes every managed row it has moved off its compiled default as
# `--<key>=<value>` (Contract 3, launcher/src/game_launch.cpp), and the game reads the
# profile itself only for `[remap]`. Measured 2026-10-05: a top-level `license_mask = 0` in a
# profile handed to `--launcher_profile` does not change the licence the game boots with, so
# there is no rank-4 "profile as a config file" step (docs/plans/launcher-plan.md section
# 11). The rows this script asserts therefore have to be ones the contract passes - and the
# ones it picks are readable back out of the boot log:
#
#   * `[launch] target`     -> --ultimate_mode / --license_mask, and the ultimate: and
#                              content licence: lines
#   * `[settings] dlc_root` -> --dlc_root, and the `dlc: N package(s) ... in <folder>` line,
#                              which names the folder the profile asked for
#   * `[settings] user_data_root` -> --user_data_root, and the log's `User data:` line
#   * `[remap]`             -> read out of the profile by the game itself, and the
#                              `remap: N pad control(s) rebound` line
#   * `[settings] mouse_ui_nav` -> --mouse_ui_nav=false. Asserted on the command line only:
#                              the driver is installed unconditionally and reads the cvar per
#                              event, so `mouse_ui: the mouse navigates the menus` is printed
#                              whether the row is on or off, and asserting that line would be
#                              asserting nothing (measured 2026-10-05).
#
# Two discrepancies this work measured are recorded rather than worked around, because
# neither is the acceptance run's to change: the profile is not loaded as a config file (so a
# game started by double-click, with no launcher in front of it, does not see the launcher's
# rows at all), and the General tab's two path rows are stored in `[launch] user_data_dir` /
# `dlc_dir` while the launch command reads `[settings] user_data_root` / `dlc_root` - so a
# folder picked in the launcher is shown by `--dump-general` and not passed to the game. This
# script asserts the spelling the contract reads, which is the one that works, and says so in
# docs/plans/launcher-plan.md section 11 rather than quietly.
#
# What it needs, and what it deliberately does not:
#
#   * a built installer in installer\out\dist (installer\README.md), a game dump to import
#     (the tree's own game\ folder, read-only) and, for the ultimate leg, the mod's folder;
#   * no controller, no clicks, no OCR and no capture. The game does need a GPU to boot, so
#     this is not headless the way the unit tests are; it is unattended, which is a different
#     thing. The controller path stays manual (docs/plans/launcher-plan.md section 6.2).
#
# Usage:
#   .\scripts\acceptance_launcher.ps1                      # install, then all four legs
#   .\scripts\acceptance_launcher.ps1 -Only retail,demo     # a subset
#   .\scripts\acceptance_launcher.ps1 -SkipInstall          # reuse the install already there
#
# Evidence lands in out/launcher-acceptance/: the install, a profile per leg, the printed
# command line per leg, the game's own log per leg, and summary.json.
#
# Exit codes: 0 every leg passed; 1 a leg failed a check; 2 the run could not start - no
# installer, no game dump, or no launcher in the install. Renaming rb_blitz_launcher.exe away
# is the documented way to see the last one, and it is 2 rather than a leg failure because
# nothing was measured.
param(
    # The setup executable to run. The default is the newest
    # installer\out\dist\RockBandBlitzSetup-*.exe.
    [string]$Installer,
    [string]$OutDir = "out/launcher-acceptance",
    # The game dump the install imports, and the DLC the profile points the game at. The
    # user's own copy; nothing here writes to either.
    [string]$GameRoot,
    # The Ultimate payload the ultimate leg installs - the mod's own folder, with
    # gen/patch_xbox.hdr and gen/patch_xbox_0.ark in it. Missing means that leg (and demo's,
    # which rides along) is skipped with a reason rather than failed.
    [string]$PayloadRoot,
    [string[]]$Only,
    [switch]$SkipInstall,
    # How long a leg waits for the game to boot before it reads the log, and how long it gives
    # the window to close on its own before killing it (the acceptance_launches.ps1 standard:
    # a leg that had to be killed is a failure, not a detail).
    [int]$BootWaitSec = 40,
    [int]$CloseWaitSec = 30
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$capDir = Join-Path $root $OutDir
$appDir = Join-Path $capDir "app"
$gameDir = Join-Path $appDir "game"
$profileDir = Join-Path $capDir "profiles"
$logDir = Join-Path $capDir "logs"
if (-not $GameRoot) { $GameRoot = Join-Path $root "game" }
if (-not $PayloadRoot) { $PayloadRoot = Join-Path $root "game\ultimate" }
$dlcRoot = Join-Path $GameRoot "dlc"

function Stop-With([int]$Code, [string]$Message) {
    Write-Host "acceptance_launcher: $Message"
    exit $Code
}

if (-not (Test-Path (Join-Path $GameRoot "default.xex"))) {
    Stop-With 2 "no default.xex under ${GameRoot} - the install has no game data to import"
}

if (-not $SkipInstall) {
    if (-not $Installer) {
        $dist = Join-Path $root "installer\out\dist"
        $newest = Get-ChildItem -LiteralPath $dist -Filter "RockBandBlitzSetup-*.exe" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($newest) { $Installer = $newest.FullName }
    }
    if (-not $Installer -or -not (Test-Path -LiteralPath $Installer)) {
        Stop-With 2 "no installer: build one (installer\build.ps1) or pass -Installer <setup.exe>"
    }
}

New-Item -ItemType Directory -Force -Path $capDir, $profileDir, $logDir | Out-Null

# ------------------------------------------------------------------ the checks ---

$checks = New-Object System.Collections.ArrayList
function Add-Check([string]$Leg, [string]$Check, [bool]$Ok, [string]$Detail = "") {
    $checks.Add([pscustomobject]@{ Leg = $Leg; Check = $Check; Ok = $Ok; Detail = $Detail }) | Out-Null
}

# ---------------------------------------------------------------- the install ---

function Invoke-Install() {
    $log = Join-Path $logDir "setup.log"
    if (Test-Path $appDir) { Remove-Item $appDir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $appDir | Out-Null
    # /RUNATEND=none because a silent install that started a game would be a window nobody
    # asked for; /GAMEMETHOD=folder because the dump is already extracted in the tree;
    # /ULTIMATESOURCE=none because the retail leg has to run *before* the mod exists.
    $arguments = @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/NOCANCEL",
        "/DIR=$appDir", "/LOG=$log", "/RUNATEND=none",
        "/GAMEMETHOD=folder", "/GAMEFOLDER=$GameRoot",
        "/ULTIMATESOURCE=none"
    )
    Write-Host "installing $Installer into $appDir"
    $process = Start-Process -FilePath $Installer -ArgumentList $arguments -Wait -PassThru
    return [pscustomobject]@{ Exit = $process.ExitCode; Log = $log }
}

if (-not $SkipInstall) {
    $install = Invoke-Install
    Add-Check "install" "the silent install exits 0" ($install.Exit -eq 0) "exit $($install.Exit), log $($install.Log)"
    if ($install.Exit -ne 0) {
        Stop-With 2 "the installer failed (exit $($install.Exit)); its log is $($install.Log)"
    }
}

$launcherExe = Join-Path $appDir "rb_blitz_launcher.exe"
$gameExe = Join-Path $appDir "rb_blitz.exe"
$manifest = Join-Path $appDir "install-manifest.toml"

if (-not (Test-Path -LiteralPath $launcherExe)) {
    # Renaming the launcher away is the documented failure: the deliverable is an install
    # whose launcher works, so nothing below can be measured without it.
    Stop-With 2 "no launcher at $launcherExe - the payload did not place one (rename it away and this is the check that notices)"
}

Add-Check "install" "the payload placed the game" (Test-Path -LiteralPath $gameExe) $gameExe
Add-Check "install" "the payload placed the launcher" (Test-Path -LiteralPath $launcherExe) $launcherExe
Add-Check "install" "the installer imported the game data" `
    (Test-Path (Join-Path $gameDir "default.xex")) (Join-Path $gameDir "default.xex")
Add-Check "install" "the install wrote its manifest" (Test-Path -LiteralPath $manifest) $manifest
if (Test-Path -LiteralPath $manifest) {
    $manifestText = Get-Content -LiteralPath $manifest -Raw
    Add-Check "install" "the manifest records where the game went" `
        ($manifestText -match "(?m)^game_directory = ") "install-manifest.toml"
    Add-Check "install" "the manifest records the Ultimate state" `
        ($manifestText -match "(?m)^ultimate_installed = ") "install-manifest.toml"
}

$dumpSha256 = ""
if (Test-Path (Join-Path $gameDir "default.xex")) {
    $dumpSha256 = (Get-FileHash -LiteralPath (Join-Path $gameDir "default.xex") -Algorithm SHA256).Hash.ToLowerInvariant()
    $givenSha256 = (Get-FileHash -LiteralPath (Join-Path $GameRoot "default.xex") -Algorithm SHA256).Hash.ToLowerInvariant()
    Add-Check "install" "the import copied the dump it was given" ($dumpSha256 -eq $givenSha256) $dumpSha256
}

# --------------------------------------------------------------- the profile ---

# One profile per leg, rendered rather than copied: what is being checked is that these
# values reach the game, so they have to be ours. Paths are written as TOML *literal*
# strings - a Windows path in a basic string is a minefield of escapes (`\r`, `\t`), and a
# hand-edited profile uses the single-quoted form for exactly that reason. The DLC row names
# the dump's own dlc folder rather than the install's, which has none: DLC is the user's
# content wherever they keep it, and pointing the row at it is what makes the packages line
# evidence rather than an absence.
function Write-Fixture([string]$Path, [string]$Target, [bool]$MouseRowOff, [string]$UserDataDir) {
    # The mouse row's compiled default is on, so "off" is the non-default direction and the one
    # the launch command has to carry.
    $mouse = if ($MouseRowOff) { "false" } else { "true" }
    $settings = @("mouse_ui_nav = $mouse", "resolution_scale = 2", "dlc_root = '$dlcRoot'")
    if ($UserDataDir) { $settings += "user_data_root = '$UserDataDir'" }
    $text = @"
schema_version = 1

[launcher]
version = 1
portable = false

[window]
width = 1280
height = 840

[launch]
target = "$Target"
game_dir = '$gameDir'
user_data_dir = ''
dlc_dir = ''

[settings]
$($settings -join "`n")

[remap]
a = "pad:b"
"@
    Set-Content -LiteralPath $Path -Value $text -NoNewline -Encoding ascii
    return $Path
}

# The launcher's own report of the command line (Contract 3). It is built by the same code a
# real start uses and printed before any window is created, which is what makes the contract
# assertable on a machine nobody is watching.
function Get-LauncherCommand([string]$Profile, [string]$File) {
    if (Test-Path -LiteralPath $File) { Remove-Item $File -Force }
    $p = Start-Process -FilePath $launcherExe -ArgumentList @("--print-command=$File", "--launcher_profile=$Profile") `
        -Wait -PassThru
    $text = if (Test-Path -LiteralPath $File) { Get-Content -LiteralPath $File -Raw } else { "" }
    return [pscustomobject]@{ Exit = $p.ExitCode; Text = $text }
}

# The command line split into arguments the way CommandLineToArgvW reads it: a run of
# non-space characters, or a quoted run in which a backslash before a quote escapes it.
# Assertions need tokens ("is this flag present?"); the run itself passes the raw remainder
# string, so nothing here can change what the game is handed.
function ConvertFrom-CommandLine([string]$Text) {
    $tokens = New-Object System.Collections.ArrayList
    $current = New-Object System.Text.StringBuilder
    $inQuotes = $false
    $started = $false
    for ($i = 0; $i -lt $Text.Length; $i++) {
        $c = $Text[$i]
        if ($c -eq '\' -and $i + 1 -lt $Text.Length -and $Text[$i + 1] -eq '"') {
            [void]$current.Append('"'); $i++; $started = $true
        } elseif ($c -eq '"') {
            $inQuotes = -not $inQuotes; $started = $true
        } elseif ($c -eq ' ' -and -not $inQuotes) {
            if ($started) { $tokens.Add($current.ToString()) | Out-Null; [void]$current.Clear(); $started = $false }
        } else {
            [void]$current.Append($c); $started = $true
        }
    }
    if ($started) { $tokens.Add($current.ToString()) | Out-Null }
    return $tokens.ToArray()
}

function Get-CommandTokens([string]$Printed) {
    return ConvertFrom-CommandLine (Get-CommandArguments $Printed)
}

# The bytes the launcher printed after `command     : ` - what is run, unresolved and
# unrequoted, so a value with a space in it stays one argument.
function Get-CommandArguments([string]$Printed) {
    if ($Printed -notmatch "(?ms)^command\s+:\s*(.*)$") { return "" }
    return $Matches[1].Trim()
}

# ------------------------------------------------------------- the game leg ---

# The game rotates its log: at trace level a 40-second boot is three files. Reading only the
# newest one reads the tail, and the boot lines are in the head.
function Get-GameLogText {
    $text = ""
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $appDir "logs") -Filter "rb_blitz_*.log" -ErrorAction SilentlyContinue |
            Sort-Object Name) {
        $text += (Get-Content -LiteralPath $file.FullName -Raw)
    }
    return $text
}

function Get-FirstLogLine([string]$Text, [string]$Pattern) {
    $line = @($Text -split "`n" | Where-Object { $_ -match $Pattern } | Select-Object -First 1)
    if ($line.Count -eq 0) { return "" }
    return ($line[0] -replace "^\[[^\]]+\] \[[^\]]+\] \[[^\]]+\] \[[^\]]+\] ", "").Trim()
}

# Starts the game with exactly the bytes the launcher printed, plus the log cvars the
# evidence needs, waits for the boot, then closes the window and reports what happened. The
# log flags are the only difference between what the launcher would start and what runs here;
# they are named in the header rather than left implicit, and they are why the log names the
# roots it mounted.
function Invoke-GameLeg([string]$Leg, [string]$Arguments, [string]$CommandFile) {
    if (-not $Arguments) {
        Add-Check $Leg "the launcher printed a command line" $false "nothing after 'command' in $CommandFile"
        return $null
    }
    $logDirectory = Join-Path $appDir "logs"
    if (Test-Path $logDirectory) {
        Get-ChildItem -LiteralPath $logDirectory -Filter "*.log" | Remove-Item -Force
    }
    $run = "$Arguments --log_noisy=true --log_level=trace --log_flush_interval=1 --log_max_file_size_mb=200"
    Write-Host "  starting: $run"
    $process = Start-Process -FilePath $gameExe -ArgumentList $run -WorkingDirectory $appDir -PassThru
    Start-Sleep -Seconds $BootWaitSec

    $alive = -not $process.HasExited
    $log = @(Get-ChildItem -LiteralPath $logDirectory -Filter "rb_blitz_*.log" -ErrorAction SilentlyContinue)
    $text = Get-GameLogText
    $fatal = $text -match "\[FATAL\]"

    $clean = $false
    if (-not $process.HasExited) {
        # CloseMainWindow is the request the title's own X sends; a process that has to be
        # killed is reported rather than hidden.
        $process.CloseMainWindow() | Out-Null
        if ($process.WaitForExit($CloseWaitSec * 1000)) { $clean = $true } else { Stop-Process -Id $process.Id -Force }
    }

    $result = [pscustomobject]@{
        Leg = $Leg; Command = $arguments; Logs = $log.Count; Bytes = $text.Length
        Alive = $alive; Fatal = $fatal; Clean = $clean
    }
    Add-Check $Leg "the game is still running after the boot" $alive "pid $($process.Id)"
    Add-Check $Leg "the game wrote a log" ($log.Count -gt 0) "logs\rb_blitz_*.log"
    Add-Check $Leg "no [FATAL] in the boot log" (-not $fatal) ""
    Add-Check $Leg "closing the window ends the process" $clean ""
    return [pscustomobject]@{ Result = $result; Text = $text }
}

# The lines every leg has to show, whatever its target: the root the game booted (named, and
# identified by the hash of the `default.xex` it loaded, so "the install's copy" is not taken
# on trust), the folder it writes its own state into, the DLC folder the profile named, and the
# pad rebinding only the profile can supply.
function Test-CommonBootLines([string]$Leg, [string]$Text) {
    Add-Check $Leg "the game booted against the install's own game folder" `
        ($Text -match ("Game directory:\s+" + [regex]::Escape($gameDir))) (Get-FirstLogLine $Text "Game directory:")
    Add-Check $Leg "the image it ran is the install's own dump" `
        ($Text -match ("game data identity: .*sha256 " + $dumpSha256)) $dumpSha256
    Add-Check $Leg "DLC comes from the folder the profile named" `
        ($Text -match "dlc: \d+ package\(s\) for title\(s\) [0-9A-F, ]+ in " + [regex]::Escape($dlcRoot)) `
        (Get-FirstLogLine $Text "dlc: ")
    Add-Check $Leg "the game read the launcher's profile (the pad rebinding)" `
        ($Text -match "remap: 1 pad control\(s\) rebound by the launcher profile") `
        (Get-FirstLogLine $Text "remap: ")
}

# The argv contract, asserted on the launcher's own text before anything is started.
function Test-CommandContract([string]$Leg, [string[]]$Tokens, [string]$Target, [string]$UserDataRoot, [bool]$MouseRowOff) {
    Add-Check $Leg "the command starts the installed game" ($Tokens[0] -eq $gameExe) $Tokens[0]
    Add-Check $Leg "it names the install's game folder" `
        ($Tokens -contains "--game_data_root=$gameDir") "--game_data_root=$gameDir"
    Add-Check $Leg "it names the profile the launcher was given" `
        (@($Tokens | Where-Object { $_ -like "--launcher_profile=*" }).Count -eq 1) `
        (@($Tokens | Where-Object { $_ -like "--launcher_profile=*" }) -join " ")
    $wantedMode = if ($Target -eq "ultimate") { "--ultimate_mode=1" } else { "--ultimate_mode=0" }
    Add-Check $Leg "the target decides --ultimate_mode" ($Tokens -contains $wantedMode) $wantedMode
    if ($Target -eq "demo") {
        Add-Check $Leg "the trial target passes --license_mask=0" ($Tokens -contains "--license_mask=0") ""
    } else {
        Add-Check $Leg "no licence mask for a purchased target" `
            (@($Tokens | Where-Object { $_ -like "--license_mask*" }).Count -eq 0) ""
    }
    if ($UserDataRoot) {
        Add-Check $Leg "the save override is passed as its own argument" `
            ($Tokens -contains "--user_data_root=$UserDataRoot") "--user_data_root=$UserDataRoot"
    } else {
        Add-Check $Leg "no save override when the profile names none" `
            (@($Tokens | Where-Object { $_ -like "--user_data_root*" }).Count -eq 0) ""
    }
    Add-Check $Leg "the DLC row travels as a flag" ($Tokens -contains "--dlc_root=$dlcRoot") "--dlc_root=$dlcRoot"
    # A row the user moved off its default travels as a flag, whatever its kind; a row left at
    # its default is *not* passed at all, which is what keeps the game's own file in charge of
    # it (D3). The mouse row is the second case: it is on by default.
    Add-Check $Leg "a changed number travels as a flag" ($Tokens -contains "--resolution_scale=2") ""
    $mouseFlags = @($Tokens | Where-Object { $_ -like "--mouse_ui_nav=*" -or $_ -eq "--no-mouse_ui_nav" })
    if ($MouseRowOff) {
        Add-Check $Leg "a bool row moved off its default travels as a flag" `
            ($mouseFlags -contains "--mouse_ui_nav=false") ($mouseFlags -join " ")
    } else {
        Add-Check $Leg "a row left at its default is not passed" ($mouseFlags.Count -eq 0) ($mouseFlags -join " ")
    }
    return $true
}

# Which legs this run wanted (-Only retail,demo), so a single leg can be re-run while the
# others are being worked on.
function New-Leg([string]$Name, [string[]]$Wanted) {
    return (-not $Wanted) -or ($Wanted -contains $Name)
}

$results = New-Object System.Collections.ArrayList

# --- retail -----------------------------------------------------------------

if (New-Leg "retail" $Only) {
    Write-Host "`n=== leg: retail (before the mod is installed) ==="
    if (-not $SkipInstall) {
        # This leg is the "without the payload" half of the Ultimate coverage, and the way it is
        # made honest is that the install this run just did put nothing there. Reusing an install
        # (-SkipInstall) cannot promise that: it may be the one a previous run installed the mod
        # into, so the folder is reported rather than asserted on.
        Add-Check "retail" "the payload's folder is not on disk yet" `
            (-not (Test-Path (Join-Path $gameDir "ultimate"))) (Join-Path $gameDir "ultimate")
    } else {
        Write-Host "  (-SkipInstall: not asserting the payload's folder is absent)"
    }
    $profile = Write-Fixture (Join-Path $profileDir "retail.toml") "common" $true ""
    $commandFile = Join-Path $logDir "retail-command.txt"
    $printed = Get-LauncherCommand $profile $commandFile
    Test-CommandContract "retail" (Get-CommandTokens $printed.Text) "common" "" $true | Out-Null
    $leg = Invoke-GameLeg "retail" (Get-CommandArguments $printed.Text) $commandFile
    if ($leg) {
        $text = $leg.Text
        Add-Check "retail" "the retail route is taken" `
            ($text -match "ultimate: off, booting the retail game data") (Get-FirstLogLine $text "ultimate: ")
        Add-Check "retail" "no payload is mounted" (-not $text.Contains("ultimate: payload ")) ""
        Add-Check "retail" "the title is treated as purchased" `
            ($text -match "content licence: license_mask = 1 \(default; the title is treated as purchased\)") `
            (Get-FirstLogLine $text "content licence:")
        Add-Check "retail" "the game reports the save folder it uses (the profile names none)" `
            ($text -match "User data:\s+\S") (Get-FirstLogLine $text "User data:")
        Test-CommonBootLines "retail" $text
        $results.Add($leg.Result) | Out-Null
    }
}

# --- the mod, through the helper B8 uses -------------------------------------

$modInstalled = $false
$payloadAvailable = Test-Path (Join-Path $PayloadRoot "gen\patch_xbox.hdr")
$wantsMod = (New-Leg "ultimate" $Only) -or (New-Leg "demo" $Only)

if ($wantsMod) {
    Write-Host "`n=== the mod, through rb_blitz_setup_helper.exe ==="
    if (-not $payloadAvailable) {
        Add-Check "install" "the Ultimate payload is available to install" $false $PayloadRoot
        Write-Host "  no payload at $PayloadRoot - the ultimate and demo legs are skipped"
    } else {
        # The install's own helper, which is the binary the launcher's *Install Ultimate*
        # button runs (B8): the install folder is its --dest, and it works out the game folder
        # inside it the same way it did when the wizard called it.
        $helper = Join-Path $appDir "rb_blitz_setup_helper.exe"
        if (-not (Test-Path -LiteralPath $helper)) {
            Add-Check "install" "the install carries the helper that adds the mod" $false $helper
        } else {
            $summary = Join-Path $logDir "install-ultimate-summary.txt"
            if (Test-Path -LiteralPath $summary) { Remove-Item $summary -Force }
            $arguments = @("install-ultimate", "--dest=$appDir", "--from-dir=$PayloadRoot",
                "--summary=$summary", "--log=$(Join-Path $logDir 'install-ultimate.log')")
            $process = Start-Process -FilePath $helper -ArgumentList $arguments -Wait -PassThru
            $summaryText = if (Test-Path -LiteralPath $summary) { (Get-Content -LiteralPath $summary -Raw).Trim() } else { "" }
            $modInstalled = (Test-Path (Join-Path $gameDir "ultimate\gen\patch_xbox.hdr")) -and
                (Test-Path (Join-Path $gameDir "ultimate\gen\patch_xbox_0.ark"))
            Add-Check "install" "the helper installs the mod from a folder" $modInstalled `
                "exit $($process.ExitCode): $summaryText"
        }
    }
}

# --- ultimate -----------------------------------------------------------------

if ((New-Leg "ultimate" $Only) -and $modInstalled) {
    Write-Host "`n=== leg: ultimate (the payload installed) ==="
    $installedPayload = Join-Path $gameDir "ultimate"
    $profile = Write-Fixture (Join-Path $profileDir "ultimate.toml") "ultimate" $false ""
    $commandFile = Join-Path $logDir "ultimate-command.txt"
    $printed = Get-LauncherCommand $profile $commandFile
    Test-CommandContract "ultimate" (Get-CommandTokens $printed.Text) "ultimate" "" $false | Out-Null
    $leg = Invoke-GameLeg "ultimate" (Get-CommandArguments $printed.Text) $commandFile
    if ($leg) {
        $text = $leg.Text
        Add-Check "ultimate" "the payload is recognised" `
            ($text.Contains("ultimate: payload $installedPayload")) (Get-FirstLogLine $text "ultimate: payload")
        Add-Check "ultimate" "the content device is re-pointed" `
            ($text -match "ultimate: content device string at 0x[0-9A-F]+ patched: UPDATE: -> D:") ""
        Add-Check "ultimate" "the overlay is mounted" `
            ($text -match "ultimate: overlay \\Device\\BlitzOverlay = ") (Get-FirstLogLine $text "ultimate: overlay")
        Add-Check "ultimate" "d: resolves to the overlay" `
            ($text.Contains("Registered symbolic link: d: => \Device\BlitzOverlay")) ""
        Add-Check "ultimate" "the retail route is not also taken" `
            (-not $text.Contains("ultimate: off, booting the retail game data")) ""
        Add-Check "ultimate" "no dirty-disc abort" (-not $text.Contains("XamShowDirtyDiscErrorUI")) ""
        Add-Check "ultimate" "the title is treated as purchased" `
            ($text -match "content licence: license_mask = 1 \(default; the title is treated as purchased\)") ""
        Test-CommonBootLines "ultimate" $text
        $results.Add($leg.Result) | Out-Null
    }
}

# --- demo ---------------------------------------------------------------------

if ((New-Leg "demo" $Only) -and $modInstalled) {
    Write-Host "`n=== leg: demo (the XBLA trial) ==="
    $profile = Write-Fixture (Join-Path $profileDir "demo.toml") "demo" $true ""
    $commandFile = Join-Path $logDir "demo-command.txt"
    $printed = Get-LauncherCommand $profile $commandFile
    Test-CommandContract "demo" (Get-CommandTokens $printed.Text) "demo" "" $true | Out-Null
    $leg = Invoke-GameLeg "demo" (Get-CommandArguments $printed.Text) $commandFile
    if ($leg) {
        $text = $leg.Text
        Add-Check "demo" "the trial licence is what the boot used" `
            ($text -match "content licence: license_mask = 0 \(configured\)") (Get-FirstLogLine $text "content licence:")
        Add-Check "demo" "the retail route is taken" `
            ($text -match "ultimate: off, booting the retail game data") (Get-FirstLogLine $text "ultimate: ")
        Test-CommonBootLines "demo" $text
        $results.Add($leg.Result) | Out-Null
    }
}

# --- save ---------------------------------------------------------------------

if (New-Leg "save" $Only) {
    Write-Host "`n=== leg: save (an isolated --user_data_root) ==="
    # A fresh writable root under out/, the way scripts/acceptance_persistence.ps1 isolates
    # one: what is checked is this override, not whatever the machine's own profile holds.
    $saveRoot = Join-Path $capDir "userdata"
    if (Test-Path -LiteralPath $saveRoot) { Remove-Item $saveRoot -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $saveRoot | Out-Null
    $profile = Write-Fixture (Join-Path $profileDir "save.toml") "common" $true $saveRoot
    $commandFile = Join-Path $logDir "save-command.txt"
    $printed = Get-LauncherCommand $profile $commandFile
    Test-CommandContract "save" (Get-CommandTokens $printed.Text) "common" $saveRoot $true | Out-Null
    $leg = Invoke-GameLeg "save" (Get-CommandArguments $printed.Text) $commandFile
    if ($leg) {
        $text = $leg.Text
        Add-Check "save" "the override is the folder the game uses" `
            ($text -match ("User data:\s+" + [regex]::Escape($saveRoot))) (Get-FirstLogLine $text "User data:")
        # Using a root is a claim about files, not about a string in a log: the title's own
        # content tree has to appear under it.
        $written = @(Get-ChildItem -LiteralPath $saveRoot -Recurse -File -ErrorAction SilentlyContinue)
        Add-Check "save" "the title wrote its storage under the override" ($written.Count -gt 0) `
            "$($written.Count) file(s) under $saveRoot"
        Add-Check "save" "the retail route is taken" `
            ($text -match "ultimate: off, booting the retail game data") ""
        Test-CommonBootLines "save" $text
        $results.Add($leg.Result) | Out-Null
    }
}

# --------------------------------------------------------------------- report ---

$failed = @($checks | Where-Object { -not $_.Ok })
Write-Host "`n=== checks ==="
$checks | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
if ($results.Count -ne 0) {
    Write-Host "=== runs ==="
    $results | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
}
Write-Host ("{0} of {1} checks passed" -f ($checks.Count - $failed.Count), $checks.Count)
foreach ($f in $failed) { Write-Host "FAILED [$($f.Leg)] $($f.Check) - $($f.Detail)" }

[pscustomobject]@{
    Installer = $Installer; Install = $appDir; GameRoot = $GameRoot; PayloadRoot = $PayloadRoot
    Launcher = $launcherExe; DlcRoot = $dlcRoot; BootWaitSec = $BootWaitSec
    Runs = $results.ToArray(); Checks = $checks.ToArray()
} | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $capDir "summary.json")

if ($failed.Count -ne 0) { exit 1 }
exit 0
