# Windows development commands; also supports dot-sourcing in PowerShell.
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'run', 'run-game', 'run-normal', 'run-window', 'run-no-dlc', 'no-dlc', 'run-portrait', 'quick', 'installer', 'build-installer', 'help', '--help', '-h')]
    [string]$Action = 'help',

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest,

    [Alias('-help', 'h')]
    [switch]$Help
)

$Global:RbBlitzRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$nativeArch = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE }
$Global:RbBlitzPreset = if ($nativeArch -eq 'ARM64') { 'win-arm64-release' } else { 'win-amd64-release' }

function Get-BuildTreeUser {
    <#
    .SYNOPSIS
        Find a cmake/ninja process that is already driving a CMake preset tree.
    .DESCRIPTION
        Both front-ends select the preset by name, and ninja's -C (like a
        configure's -B) names the build directory, so either string identifies
        a live user of the tree. Nothing found means the tree is free.
    #>
    param(
        [string]$Preset = $Global:RbBlitzPreset,
        [string]$BuildDir
    )

    $needles = @($Preset)
    if ($BuildDir) { $needles += $BuildDir.Replace('\', '/') }

    Get-CimInstance Win32_Process -Filter "Name = 'cmake.exe' OR Name = 'ninja.exe'" -ErrorAction SilentlyContinue |
        Where-Object {
            $proc = $_
            if (-not $proc.CommandLine) { return $false }
            foreach ($needle in $needles) {
                if ($proc.CommandLine.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    return $true
                }
            }
            return $false
        } |
        Select-Object -First 1
}

function Build-Blitz {
    <#
    .SYNOPSIS
        Build the release game tree (rb_blitz.exe).
    .DESCRIPTION
        Runs `cmake --build --preset win-amd64-release`. Use -Configure to
        re-run `cmake --preset win-amd64-release` first (needed after a
        Vulkan-off test configure clobbers the shared SDK objects, and on a
        fresh checkout). Extra arguments go to `cmake --build`.

        Refuses to start while another cmake/ninja is already driving the
        release tree, and while rb_blitz.exe is running (the link cannot
        replace a locked executable). A failed configure or build is reported
        as an error instead of only setting $LASTEXITCODE.
    #>
    [CmdletBinding()]
    param(
        [switch]$Configure,
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Extra
    )

    $root = $Global:RbBlitzRoot
    $buildDir = Join-Path $root ("out/build/" + $Global:RbBlitzPreset)
    $cache = Join-Path $buildDir 'CMakeCache.txt'

    # Two processes on one build tree are what turns a build into the opaque
    # "ninja: error: failed recompaction: Permission denied": cmake ends a
    # generate with `ninja -t recompact`, and the running build's ninja keeps
    # .ninja_log mapped, so the rewrite is refused and the configure dies.
    # VS Code's CMake Tools configures this same folder, so refuse up front
    # instead of letting one front-end break the other.
    $busy = Get-BuildTreeUser -BuildDir $buildDir
    if ($busy) {
        Write-Error ("the release tree is already in use by {0} (pid {1}: {2}). Wait for it to finish, then retry." -f $busy.Name, $busy.ProcessId, $busy.CommandLine)
        return
    }

    $game = Get-Process rb_blitz -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($game) {
        Write-Error ("rb_blitz.exe is running (pid {0}); the link would fail because the executable is locked. Close the game, then retry." -f $game.Id)
        return
    }

    Push-Location $root
    try {
        if ($Configure -or -not (Test-Path $cache)) {
            & cmake --preset $Global:RbBlitzPreset
            if ($LASTEXITCODE -ne 0) {
                Write-Error ("configuring the release tree failed (cmake --preset exited {0})." -f $LASTEXITCODE)
                return
            }
        }
        & cmake --build --preset $Global:RbBlitzPreset @Extra
        if ($LASTEXITCODE -ne 0) {
            Write-Error ("the build failed (cmake --build exited {0})." -f $LASTEXITCODE)
        }
    }
    finally {
        Pop-Location
    }
}

function Run-Blitz {
    <#
    .SYNOPSIS
        Run the built game with the local game_data_root.
    .DESCRIPTION
        Changes into out\build\win-amd64-release (so the title's relative paths
        and logs behave as usual) and starts rb_blitz.exe with
        --game_data_root=..\..\..\game. Launch tags can be combined; other
        arguments are forwarded after the tag defaults. Use --help for details.
    #>
    [CmdletBinding()]
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Extra
    )

    if (@($Extra | Where-Object { $_ -in @('--help', '-h') }).Count) {
        Get-BlitzHelp
        return
    }

    $defaults = @()
    $forward = @()
    $portrait = $false
    $quick = $false
    foreach ($arg in $Extra) {
        switch ($arg) {
            '--normal' { $defaults += '--ultimate_mode=0' }
            '--window' { $defaults += '--fullscreen=false' }
            '--windowed' { $defaults += '--fullscreen=false' }
            '--no-dlc' {
                $emptyDlc = Join-Path $Global:RbBlitzRoot 'out\no-dlc'
                New-Item -ItemType Directory -Force -Path $emptyDlc -ErrorAction Stop | Out-Null
                $defaults += "--dlc_library=$emptyDlc", "--dlc_root=$emptyDlc"
            }
            '--portrait' { $portrait = $true }
            '--quick' { $quick = $true }
            default { $forward += $arg }
        }
    }
    if (-not $quick) {
        $defaults += @(Get-BlitzDlcArguments -Extra @($defaults + $forward))
    }
    if ($portrait) {
        $prepared = New-BlitzPortraitArguments -Extra $forward
        $defaults += '--ui.fullscreen=false'
        $forward = @($prepared)
    }
    if ($quick) {
        # Start-Process joins arguments into a command line rather than quoting
        # each item. Paths generated here may contain spaces.
        $quickArgs = @($defaults + $forward | ForEach-Object {
            if ($_ -match '\s' -and $_ -notmatch '"') { '"' + $_ + '"' } else { $_ }
        })
        Start-BlitzQuick @quickArgs
        return
    }

    $buildDir = Join-Path $Global:RbBlitzRoot ("out/build/" + $Global:RbBlitzPreset)
    $exe = Join-Path $buildDir 'rb_blitz.exe'

    if (-not (Test-Path $exe)) {
        Write-Error "rb_blitz.exe not found in '$buildDir'. Run 'build-game' (or 'bash ./dev.sh build') first."
        return
    }

    Push-Location $buildDir
    try {
        & $exe '--game_data_root=..\..\..\game' @defaults @forward
    }
    finally {
        Pop-Location
    }
}

function Get-BlitzDlcArguments {
    # Inherit only the installed DLC root, keeping development saves independent.
    param([string[]]$Extra)

    if (@($Extra | Where-Object {
        $_ -match '^--(?:blitz\.runtime\.)?dlc_(?:root|library)(?:=|$)'
    }).Count -or $env:REX_DLC_ROOT -or $env:REX_DLC_LIBRARY -or
        $env:REX_BLITZ_RUNTIME_DLC_ROOT -or $env:REX_BLITZ_RUNTIME_DLC_LIBRARY) { return }

    $buildDir = Join-Path $Global:RbBlitzRoot ("out/build/" + $Global:RbBlitzPreset)
    $pathFiles = @(Join-Path $buildDir 'paths.toml')
    $uninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{92C7B4E1-3F5A-4D2E-9B18-7A6C4E0D5F31}_is1'
    foreach ($registryRoot in @('HKCU:', 'HKLM:', 'HKLM:\Software\WOW6432Node')) {
        $key = if ($registryRoot -like '*WOW6432Node') {
            Join-Path $registryRoot ($uninstallKey -replace '^Software\\', '')
        } else { Join-Path $registryRoot $uninstallKey }
        $installDir = (Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue).InstallLocation
        if ($installDir) { $pathFiles += Join-Path $installDir 'paths.toml' }
    }
    $pathFiles += Join-Path $env:LOCALAPPDATA 'Programs\Rock Band Blitz\paths.toml'
    $profileDir = Join-Path $env:APPDATA 'rb_blitz'
    $pointer = Join-Path $profileDir 'settings_dir.txt'
    if (Test-Path -LiteralPath $pointer) {
        $chosenDir = (Get-Content -LiteralPath $pointer -Raw -ErrorAction Stop).Trim()
        if ($chosenDir) { $profileDir = $chosenDir }
    }
    $profile = if ($env:RBBLITZ_CONTROLLER_SETTINGS_PROFILE) {
        $env:RBBLITZ_CONTROLLER_SETTINGS_PROFILE
    } else { Join-Path $profileDir 'launcher.toml' }
    for ($i = 0; $i -lt $Extra.Count; $i++) {
        if ($Extra[$i] -match '^--(?:blitz\.runtime\.)?launcher_profile=(.*)$') { $profile = $Matches[1] }
        elseif ($Extra[$i] -in @('--launcher_profile', '--blitz.runtime.launcher_profile') -and $i + 1 -lt $Extra.Count) {
            $profile = $Extra[++$i]
        }
    }
    if (-not [IO.Path]::IsPathRooted($profile)) { $profile = Join-Path $buildDir $profile }

    # Parse with TOML string rules, including escaped Windows paths.
    # Python 3.11+ is already required by the checkout's build tools.
    $reader = @'
import json, sys, tomllib
from pathlib import Path
profile, *files = sys.argv[1:]
for index, name in enumerate([*files, profile]):
    file = Path(name)
    if not file.is_file():
        continue
    try:
        with file.open('rb') as stream:
            doc = tomllib.load(stream)
    except (OSError, ValueError) as error:
        print(f'Cannot read DLC configuration {file}: {error}', file=sys.stderr)
        continue
    legacy = index == len(files)
    value = (doc.get('settings', {}).get('dlc_root') or doc.get('launch', {}).get('dlc_dir')) if legacy else doc.get('paths', {}).get('dlc_root')
    if not isinstance(value, str) or not value:
        continue
    root = Path(value)
    if not root.is_absolute():
        root = file.parent / root
    if not root.is_dir():
        print(f'DLC folder from {file} does not exist: {root}', file=sys.stderr)
    # A configured checkout root remains authoritative, including an unavailable drive.
    if index == 0:
        break
    if root.is_dir():
        print(json.dumps({'root': str(root.resolve()), 'source': str(file)}))
        break
'@
    $result = & python -c $reader $profile @pathFiles
    if ($LASTEXITCODE -ne 0) { throw 'Could not read DLC configuration (Python 3.11+ required).' }
    if ($result) {
        $inherited = $result | ConvertFrom-Json
        Write-Host "DLC root: $($inherited.root) (from $($inherited.source))"
        "--dlc_root=$($inherited.root)"
    }
}

function New-BlitzPortraitArguments {
    # Orientation has no engine flag. Use the supported profile route with a
    # fresh settings copy for each run; never rewrite the source profile.
    param([string[]]$Extra)

    $buildDir = Join-Path $Global:RbBlitzRoot ("out/build/" + $Global:RbBlitzPreset)
    $sourceProfile = $env:RBBLITZ_CONTROLLER_SETTINGS_PROFILE
    $forward = @()
    for ($i = 0; $i -lt $Extra.Count; $i++) {
        if ($Extra[$i] -match '^--(?:blitz\.runtime\.)?launcher_profile=(.*)$') {
            $sourceProfile = $Matches[1]
        } elseif ($Extra[$i] -in @('--launcher_profile', '--blitz.runtime.launcher_profile')) {
            if (++$i -ge $Extra.Count) { throw '--launcher_profile requires a path.' }
            $sourceProfile = $Extra[$i]
        } else {
            $forward += $Extra[$i]
        }
    }
    if (-not $sourceProfile) {
        $defaultDir = Join-Path $env:APPDATA 'rb_blitz'
        $pointer = Join-Path $defaultDir 'settings_dir.txt'
        $chosenDir = if (Test-Path -LiteralPath $pointer) {
            (Get-Content -LiteralPath $pointer -Raw -ErrorAction Stop).Trim()
        }
        if ($chosenDir) {
            $sourceProfile = Join-Path $chosenDir 'launcher.toml'
        } elseif ((Test-Path (Join-Path $buildDir 'rb_blitz_controller_settings.portable')) -or
                  (Test-Path (Join-Path $buildDir 'rb_blitz_launcher.portable'))) {
            $sourceProfile = Join-Path $buildDir 'launcher.toml'
        } else {
            $sourceProfile = Join-Path $defaultDir 'launcher.toml'
        }
    }
    if (-not [IO.Path]::IsPathRooted($sourceProfile)) {
        $sourceProfile = Join-Path $buildDir $sourceProfile
    }
    $sourceDir = Split-Path -Parent $sourceProfile
    $settingsName = if ([IO.Path]::GetFileName($sourceProfile) -ceq 'launcher.toml') {
        'settings.toml'
    } else {
        [IO.Path]::GetFileNameWithoutExtension($sourceProfile) + '.game-settings.toml'
    }
    $sourceSettings = Join-Path $sourceDir $settingsName
    $text = if (Test-Path -LiteralPath $sourceSettings) {
        Get-Content -LiteralPath $sourceSettings -Raw -ErrorAction Stop
    } else {
        "schema_version = 1`r`n"
    }

    # Change only these scalar keys in the copied tables, keeping other settings.
    foreach ($section in @('graphics', 'provenance')) {
        $values = if ($section -eq 'graphics') {
            [ordered]@{ orientation = "'portrait'"; aspect_ratio = "'16:9'"; force_16_by_9 = "true" }
        } else {
            [ordered]@{ "'graphics.orientation'" = "'manual'"; "'graphics.aspect_ratio'" = "'manual'"; "'graphics.force_16_by_9'" = "'manual'" }
        }
        $table = [regex]::Match($text, '(?ms)^\[' + $section + '\][^\r\n]*\r?\n(?<body>.*?)(?=^\[|\z)')
        $body = if ($table.Success) { $table.Groups['body'].Value } else { '' }
        foreach ($key in $values.Keys) {
            $keyPattern = if ($section -eq 'provenance') {
                '[\x27"]' + [regex]::Escape($key.Trim("'")) + '[\x27"]'
            } else { [regex]::Escape($key) }
            $line = $key + ' = ' + $values[$key]
            $pattern = '(?m)^\s*' + $keyPattern + '\s*=[^\r\n]*'
            if ([regex]::IsMatch($body, $pattern)) {
                $body = [regex]::Replace($body, $pattern, $line)
            } else { $body = $body.TrimEnd() + "`r`n" + $line + "`r`n" }
        }
        $replacement = '[' + $section + "]`r`n" + $body
        if ($table.Success) {
            $text = $text.Substring(0, $table.Index) + $replacement + $text.Substring($table.Index + $table.Length)
        } else { $text = $text.TrimEnd() + "`r`n`r`n" + $replacement }
    }

    $sessionDir = Join-Path $Global:RbBlitzRoot ('out\dev-portrait\' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $sessionDir -ErrorAction Stop | Out-Null
    if (Test-Path -LiteralPath $sourceProfile) {
        Copy-Item -LiteralPath $sourceProfile -Destination (Join-Path $sessionDir 'launcher.toml') -ErrorAction Stop
    }
    [IO.File]::WriteAllText((Join-Path $sessionDir 'settings.toml'), $text, [Text.UTF8Encoding]::new($false))
    Write-Host "Portrait 9:16 (16:9 source); settings copy: $sessionDir"
    "--launcher_profile=$(Join-Path $sessionDir 'launcher.toml')"
    $forward
}

function Run-BlitzWindow {
    <#
    .SYNOPSIS
        Run the built game in a desktop window.
    .DESCRIPTION
        Same as Run-Blitz, but with --fullscreen=false so the title runs in a
        window instead of a fullscreen surface. Extra arguments are forwarded.
    #>
    [CmdletBinding()]
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Extra
    )

    Run-Blitz '--window' @Extra
}

function Run-BlitzPortrait {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Extra)
    Run-Blitz '--portrait' @Extra
}

function Run-BlitzNormal {
    <#
    .SYNOPSIS
        Run the retail game even when an Ultimate payload is installed.
    .DESCRIPTION
        Same as Run-Blitz, but with --ultimate_mode=0 to disable the
        automatically detected Ultimate payload. Extra arguments are forwarded.
    #>
    [CmdletBinding()]
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Extra
    )

    Run-Blitz '--normal' @Extra
}

function Run-BlitzNoDlc {
    <#
    .SYNOPSIS
        Run the built game with an empty DLC library.
    .DESCRIPTION
        Same as Run-Blitz (fullscreen, local game_data_root), but
        --dlc_library and --dlc_root name an empty folder under out\no-dlc,
        so neither the DLC root from paths.toml nor rb_blitz.toml's library
        is scanned and the title sees no DLC packages. Both switches are
        needed: --dlc_library only adds a root. Extra arguments are
        forwarded after them, so a caller can still override either switch.
    #>
    [CmdletBinding()]
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Extra
    )

    Run-Blitz '--no-dlc' @Extra
}

function Run-BlitzQuick {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Extra)
    Run-Blitz '--quick' @Extra
}

function Start-BlitzQuick {
    <#
    .SYNOPSIS
        Run the built game windowed, without a DLC library, and skip the title screen.
    .DESCRIPTION
        A development shortcut for reaching the main menu quickly:

          * --fullscreen=false, so the title runs in a desktop window;
          * --dlc_library and --dlc_root point at an empty folder under
            out\quick-dlc, so the developer's paths.toml DLC root is not scanned
            (this is the fast-boot path the acceptance harnesses already use);
          * --enhancements_skip_offline_dialog=true, so the start press lands on
            the main menu rather than in the two offline dialogs;
          * after -BootDelaySec seconds (default 28, the measured boot-to-title
            time in scripts/diagnostics/ui_states.ps1), scripts/diagnostics/drive_ui.ps1 presses A once
            to leave the splash/title screen.

        Pass -NoSkipSplash to keep the title screen. The process is started and
        the function returns immediately; extra arguments are forwarded to
        rb_blitz.exe.
    #>
    [CmdletBinding()]
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Extra
    )

    # -BootDelaySec and -NoSkipSplash are read here rather than declared as
    # parameters: every other argument must reach rb_blitz.exe untouched, and
    # a typed positional parameter would swallow a forwarded flag first.
    $bootDelaySec = 28
    $skipSplash = $true
    $forward = @()
    $extraArgs = @()
    if ($null -ne $Extra) { $extraArgs = @($Extra) }
    for ($i = 0; $i -lt $extraArgs.Count; $i++) {
        $arg = $extraArgs[$i]
        if ($arg -match '^--?BootDelaySec=(.+)$') { $bootDelaySec = [int]$Matches[1]; continue }
        if ($arg -match '^--?BootDelaySec$') {
            if ($i + 1 -lt $extraArgs.Count) { $bootDelaySec = [int]$extraArgs[++$i] }
            continue
        }
        if ($arg -match '^--?NoSkipSplash$') { $skipSplash = $false; continue }
        $forward += $arg
    }

    $buildDir = Join-Path $Global:RbBlitzRoot ("out/build/" + $Global:RbBlitzPreset)
    $exe = Join-Path $buildDir 'rb_blitz.exe'

    if (-not (Test-Path $exe)) {
        Write-Error "rb_blitz.exe not found in '$buildDir'. Run 'build-game' (or 'bash ./dev.sh build') first."
        return
    }

    $running = Get-Process rb_blitz -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($running -and $skipSplash) {
        # drive_ui.ps1 focuses the first rb_blitz process it finds, so a second
        # instance would receive the A meant for this one.
        Write-Error "rb_blitz is already running (pid $($running.Id)). Close it, or pass -NoSkipSplash to launch without the auto-start key."
        return
    }

    # The empty folder is the whole "no DLC path" part: --dlc_library alone only
    # adds a root, so both switches must name it to override paths.toml's root.
    $emptyDlc = Join-Path $Global:RbBlitzRoot 'out\quick-dlc'
    New-Item -ItemType Directory -Force -Path $emptyDlc | Out-Null

    $arguments = @(
        '--game_data_root=..\..\..\game',
        '--fullscreen=false',
        "--dlc_library=`"$emptyDlc`"",
        "--dlc_root=`"$emptyDlc`"",
        '--enhancements_skip_offline_dialog=true',
        # drive_ui.ps1's key:a is the MnK driver's Space -> A; keep that path on
        # so the helper does not depend on a local rb_blitz.toml setting.
        '--mnk_mode=true'
    )
    if ($forward.Count -gt 0) { $arguments += $forward }

    Push-Location $buildDir
    try {
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -PassThru
        Write-Host ("run-game-quick: rb_blitz.exe (pid {0}) windowed, empty DLC library '{1}'" -f $process.Id, $emptyDlc)

        if ($skipSplash) {
            $drive = Join-Path $Global:RbBlitzRoot 'scripts\diagnostics\drive_ui.ps1'
            $helperArgs = @(
                '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $drive,
                '-Actions', ("wait:{0},key:a,wait:2" -f $bootDelaySec),
                '-BuildDir', ('out/build/' + $Global:RbBlitzPreset),
                '-OutDir', 'out/quick-boot'
            )
            Start-Process -FilePath 'powershell' -ArgumentList $helperArgs -WindowStyle Hidden
            Write-Host ("run-game-quick: leaving the title screen in {0}s (-NoSkipSplash disables this)" -f $bootDelaySec)
        }
    }
    finally {
        Pop-Location
    }
}

function Invoke-InstallerBuild {
    <#
    .SYNOPSIS
        Build the installer (installer\build.ps1).
    .DESCRIPTION
        Builds the current game tree, then runs the installer pipeline with
        Windows PowerShell 5.1. Extra arguments are forwarded to build.ps1.
    #>
    [CmdletBinding()]
    param(
        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]]$Extra
    )

    $script = Join-Path $Global:RbBlitzRoot 'installer\build.ps1'

    Push-Location $Global:RbBlitzRoot
    try {
        Build-Blitz -ErrorAction Stop
        & powershell -ExecutionPolicy Bypass -NoProfile -File $script @Extra
        if ($LASTEXITCODE -ne 0) {
            throw ("the installer build failed (installer\\build.ps1 exited {0})." -f $LASTEXITCODE)
        }
    }
    finally {
        Pop-Location
    }
}

function Get-BlitzHelp {
    @'
Usage:
  bash ./dev.sh run [game] [options] [engine arguments]
  bash ./dev.sh build [-Configure] [cmake arguments]
  bash ./dev.sh build-installer [installer arguments]
  bash ./dev.sh --help

In Bash, load with: source ./dev.sh
  run-game [options] [engine arguments]
  build-game [-Configure] [cmake arguments]
  build-installer [installer arguments]
  dev-help

Launch options (combine as needed):
  --portrait       Windowed 9:16 portrait using the game's 16:9 source/layout.
                   Copies the selected profile/settings into out\dev-portrait
                   each run. Changes there do not update the source settings.
  --window         Desktop window (--windowed is also accepted).
  --no-dlc         Empty DLC library; overrides both DLC roots.
  --normal         Retail game even when Ultimate is installed.
  --quick          Windowed, no DLC, skip offline dialogs, auto-press A at title.
                   -BootDelaySec N   Delay before A (default 28 seconds).
                   -NoSkipSplash     Keep the title screen; no automatic input.
  --help, -h       Show help without launching or creating settings.

Other arguments go to rb_blitz.exe after the option defaults, allowing overrides.
All launches use --game_data_root=..\..\..\game from the release build folder.
DLC uses the checkout paths.toml, falling back to installed paths.toml or the legacy
profile when no checkout DLC root is configured. Explicit DLC arguments take priority.
Builds refuse to run while another build or the game uses the release tree.

Examples:
  bash ./dev.sh run --portrait --no-dlc
  bash ./dev.sh run game --no-dlc
  run-game --portrait --normal --no-dlc
  run-game --window --vsync=false
  run-game --quick -NoSkipSplash
  run-game --help

Existing aliases still work:
  run-game-normal, run-game-window, run-game-no-dlc, run-game-quick
  run-game-portrait
Dispatcher shortcuts:
  run-game, run-normal, run-window, run-no-dlc, no-dlc, run-portrait, quick
  installer (same as build-installer), help
'@
}

Set-Alias -Name build-game -Value Build-Blitz -Force
Set-Alias -Name run-game -Value Run-Blitz -Force
Set-Alias -Name run-game-normal -Value Run-BlitzNormal -Force
Set-Alias -Name run-game-window -Value Run-BlitzWindow -Force
Set-Alias -Name run-game-quick -Value Run-BlitzQuick -Force
Set-Alias -Name run-game-no-dlc -Value Run-BlitzNoDlc -Force
Set-Alias -Name run-game-portrait -Value Run-BlitzPortrait -Force
Set-Alias -Name build-installer -Value Invoke-InstallerBuild -Force
Set-Alias -Name dev-help -Value Get-BlitzHelp -Force

# In an interactive session, show all matching aliases when Tab is pressed
# instead of cycling through them one at a time. This lasts only for the
# current PowerShell session, just like the aliases above.
if ($MyInvocation.InvocationName -eq '.' -and (Get-Module PSReadLine)) {
    Set-PSReadLineKeyHandler -Key Tab -Function MenuComplete
}

# Executed (not dot-sourced): dispatch once instead of leaving definitions behind.
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    if ($Help) { Get-BlitzHelp; return }
    switch ($Action) {
        'build' {
            $configure = @($Rest | Where-Object { $_ -in @('--configure', '-Configure') }).Count -ne 0
            $buildArgs = @($Rest | Where-Object { $_ -notin @('--configure', '-Configure') })
            Build-Blitz -Configure:$configure @buildArgs
        }
        'run' {
            if ($Rest.Count -gt 0 -and $Rest[0] -eq 'game') {
                $Rest = @($Rest | Select-Object -Skip 1)
            }
            Run-Blitz @Rest
        }
        'run-game' { Run-Blitz @Rest }
        'run-normal' { Run-BlitzNormal @Rest }
        'run-window' { Run-BlitzWindow @Rest }
        'run-no-dlc' { Run-BlitzNoDlc @Rest }
        'no-dlc' { Run-BlitzNoDlc @Rest }
        'run-portrait' { Run-BlitzPortrait @Rest }
        'quick' { Run-BlitzQuick @Rest }
        'installer' { Invoke-InstallerBuild @Rest }
        'build-installer' { Invoke-InstallerBuild @Rest }
        default { Get-BlitzHelp }
    }
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
