# Prepare and record the Messages acceptance matrix. This script never starts or
# drives the game: the owner performs the live routes and records observations.
#
# powershell -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/acceptance_messages.ps1
# powershell -NoProfile -ExecutionPolicy Bypass -File scripts/acceptance/acceptance_messages.ps1 -Mode Record -Leg Feed -Verdict pass -Evidence out/messages-acceptance/run/feed.log,out/messages-acceptance/run/feed.png
#
# Preparation writes isolated roots, fixtures, exact launch commands and a
# summary.json with unmeasured verdicts. It refuses to overwrite an existing
# run unless -Reuse is passed. No external URL is fetched by this script.
param(
    [ValidateSet('Prepare', 'Record', 'Summary')][string]$Mode = 'Prepare',
    [string]$BuildDir = 'out/build/win-amd64-release',
    [string]$OutDir = 'out/messages-acceptance',
    [string]$GameRoot = 'game',
    [ValidateRange(1, 65535)][int]$Port = 18787,
    [ValidateSet('Feed', 'Icon', 'Cache', 'Bundled', 'NoValidFeed', 'UpdateLater',
                 'UpdateOpen', 'UpdateFailure', 'Current', 'Pages')]
    [string]$Leg,
    [ValidateSet('pass', 'fail', 'unmeasured')][string]$Verdict,
    [string[]]$Evidence,
    [string]$Note = '',
    [switch]$Reuse
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Resolve-UnderRoot([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) { return [System.IO.Path]::GetFullPath($Path) }
    return [System.IO.Path]::GetFullPath((Join-Path $root $Path))
}
$build = Resolve-UnderRoot $BuildDir
$out = Resolve-UnderRoot $OutDir
$game = Resolve-UnderRoot $GameRoot
$summaryPath = Join-Path $out 'summary.json'
$legs = @('Feed', 'Icon', 'Cache', 'Bundled', 'NoValidFeed',
          'UpdateLater', 'UpdateOpen', 'UpdateFailure', 'Current', 'Pages')
$releaseUrl = 'https://github.com/gabrielofavero/rb-blitz-xenon-recomp/releases'
$pagesUrl = 'https://gabrielofavero.github.io/rb-blitz-xenon-recomp/feed.json'

function Write-Utf8([string]$Path, [string]$Value) {
    [System.IO.File]::WriteAllText($Path, $Value, (New-Object System.Text.UTF8Encoding($false)))
}
function Write-Json([string]$Path, $Value) {
    Write-Utf8 $Path ($Value | ConvertTo-Json -Depth 12)
}
function Read-Summary {
    if (-not (Test-Path -LiteralPath $summaryPath)) { throw "Missing $summaryPath; run -Mode Prepare." }
    return Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
}
function Save-Summary($Value) { Write-Json $summaryPath $Value }
function New-Feed($Template, [string]$Version) {
    $copy = $Template | ConvertTo-Json -Depth 12 | ConvertFrom-Json
    $copy.latest_version = $Version
    return $copy
}
function New-Args([string]$ExeDir, [string]$RootName, [string]$LogName,
                  [string]$FeedUrl, [string]$Opener) {
    $args = @(
        "--game_data_root=$game", '--ultimate_mode=1', '--mnk_mode=1',
        "--user_data_root=$(Join-Path $out "roots/$RootName")",
        "--launcher_profile=$(Join-Path $out "roots/$RootName/launcher.toml")",
        "--dlc_root=$(Join-Path $out 'dlc')",
        "--messages_api_url=$FeedUrl",
        "--log_file=$(Join-Path $out "run/$LogName.log")",
        '--log_level=info', '--log_flush_interval=1', '--log_max_file_size_mb=200'
    )
    if ($Opener) { $args += "--messages_browser_opener_path=$Opener" }
    return [ordered]@{ executable = (Join-Path $ExeDir 'rb_blitz.exe'); arguments = $args }
}
function Quote-Arg([string]$Value) {
    return '"' + $Value.Replace('"', '""') + '"'
}
function Command-Line($Launch) {
    return '& ' + (Quote-Arg $Launch.executable) + ' ' +
        (($Launch.arguments | ForEach-Object { Quote-Arg $_ }) -join ' ')
}
function Add-RunbookLeg([System.Collections.Generic.List[string]]$Lines,
                         [string]$Name, [string]$Instructions, $Launch) {
    $Lines.Add("## $Name")
    $Lines.Add('')
    $Lines.Add($Instructions)
    $Lines.Add('')
    if ($Launch) {
        $Lines.Add('Run from the build directory after starting or stopping the server as described:')
        $Lines.Add('')
        $Lines.Add('    ' + (Command-Line $Launch))
        $Lines.Add('')
    }
    $Lines.Add("Save screenshots under run/$Name-*.png and the log at run/$Name.log.")
    $Lines.Add('')
}

if ($Mode -eq 'Prepare') {
    if ((Test-Path -LiteralPath $out) -and -not $Reuse) {
        throw "Evidence directory exists: $out. Choose a new -OutDir or pass -Reuse."
    }
    if ((Test-Path -LiteralPath $summaryPath) -and $Reuse) {
        $existing = Read-Summary
        foreach ($name in $legs) {
            if ($existing.matrix.$name.verdict -ne 'unmeasured' -or
                @($existing.matrix.$name.evidence).Count -gt 0) {
                throw "Recorded evidence exists for $name; use a new -OutDir."
            }
        }
        if (@(Get-ChildItem -LiteralPath (Join-Path $out 'run') -File -ErrorAction SilentlyContinue).Count -gt 0) {
            throw "Run files exist in $out; use a new -OutDir."
        }
    }
}
if ($Mode -eq 'Prepare') {
    foreach ($path in @($out, (Join-Path $out 'fixtures'), (Join-Path $out 'roots'),
                        (Join-Path $out 'run'), (Join-Path $out 'dlc'))) {
        New-Item -ItemType Directory -Force -Path $path | Out-Null
    }
    $exe = Join-Path $build 'rb_blitz.exe'
    $bundle = Join-Path $build 'assets/messages/feed.json'
    $pins = Join-Path $root 'installer/config/pins.toml'
    if (-not (Test-Path -LiteralPath $exe)) { throw "Build first: $exe is missing." }
    if (-not (Test-Path -LiteralPath $bundle)) { throw "Bundled feed missing: $bundle" }
    if (-not (Test-Path -LiteralPath (Join-Path $game 'default.xex'))) { throw "Missing game/default.xex under $game" }
    $pinsText = Get-Content -LiteralPath $pins -Raw
    $match = [regex]::Match($pinsText, '(?ms)^\[installer\]\s*.*?^version\s*=\s*"([0-9]{1,5}\.[0-9]{1,5}\.[0-9]{1,5})"')
    if (-not $match.Success) { throw 'Cannot read [installer] version from pins.toml.' }
    $version = $match.Groups[1].Value
    $parts = $version.Split('.')
    $newer = ([int]$parts[0] + 1).ToString() + '.' + $parts[1] + '.' + $parts[2]
    $fixture = Get-Content -LiteralPath (Join-Path $root 'tests/fixtures/messages/two/feed.json') -Raw | ConvertFrom-Json
    if ($fixture.messages.Count -ne 2 -or $fixture.messages[0].icon -eq $fixture.messages[1].icon) {
        throw 'The two-row fixture must carry two different built-in icons.'
    }
    Write-Json (Join-Path $out 'fixtures/current.json') (New-Feed $fixture $version)
    Write-Json (Join-Path $out 'fixtures/newer.json') (New-Feed $fixture $newer)
    Copy-Item -LiteralPath $bundle -Destination (Join-Path $out 'fixtures/bundled.json') -Force
    Write-Utf8 (Join-Path $out 'fixtures/invalid.json') '{"schema_version":999,"messages":[]}'
    $validator = Join-Path $root 'scripts/messages/check_feed.mjs'
    foreach ($feed in @($bundle, (Join-Path $out 'fixtures/current.json'))) {
        & node $validator --feed $feed | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "Feed validation failed: $feed" }
    }
    foreach ($name in @('online', 'bundled', 'no-valid-feed', 'update-later',
                        'update-open', 'update-failure', 'current')) {
        $dir = Join-Path $out "roots/$name"
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        Write-Utf8 (Join-Path $dir 'launcher.toml') @'
schema_version = 1

[launcher]
version = 1
portable = false

[launch]
target = "ultimate"
'@
        Write-Utf8 (Join-Path $dir 'settings.toml') @'
schema_version = 1

[interface]
show_messages = true
'@
    }
    # A second executable directory keeps the normal build's shipped feed intact.
    # Only the runtime files needed beside rb_blitz.exe are copied; game_data_root
    # remains the explicit game directory above.
    $isolated = Join-Path $out 'no-bundle-build'
    New-Item -ItemType Directory -Force -Path $isolated | Out-Null
    foreach ($name in @('rb_blitz.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'rb_blitz.toml')) {
        $src = Join-Path $build $name
        if (Test-Path -LiteralPath $src) { Copy-Item -LiteralPath $src -Destination $isolated -Force }
    }
    $noBundle = Join-Path $isolated 'assets/messages/feed.json'
    if (Test-Path -LiteralPath $noBundle) { throw "Expected no bundled feed in $isolated" }
    $offlineUrl = "http://127.0.0.1:$Port/feed.json"
    $launch = [ordered]@{}
    $launch.Feed = New-Args $build 'online' 'Feed' $offlineUrl ''
    $launch.Icon = New-Args $build 'online' 'Icon' $offlineUrl ''
    $launch.Cache = New-Args $build 'online' 'Cache' $offlineUrl ''
    $launch.Bundled = New-Args $build 'bundled' 'Bundled' $offlineUrl ''
    $launch.NoValidFeed = New-Args $isolated 'no-valid-feed' 'NoValidFeed' $offlineUrl ''
    $launch.UpdateLater = New-Args $build 'update-later' 'UpdateLater' $offlineUrl ''
    $stub = Join-Path $build 'rb_blitz_messages_browser_stub.exe'
    if (-not (Test-Path -LiteralPath $stub)) { throw "Build the acceptance opener target first: $stub" }
    $launch.UpdateOpen = New-Args $build 'update-open' 'UpdateOpen' $offlineUrl $stub
    $launch.UpdateFailure = New-Args $build 'update-failure' 'UpdateFailure' $offlineUrl (Join-Path $out 'missing-browser-opener.exe')
    $launch.Current = New-Args $build 'current' 'Current' $offlineUrl ''
    Write-Json (Join-Path $out 'launches.json') $launch
    $server = Join-Path $PSScriptRoot 'messages_feed_server.py'
    $serverCurrent = "python $(Quote-Arg $server) --feed $(Quote-Arg (Join-Path $out 'fixtures/current.json')) --port $Port --log $(Quote-Arg (Join-Path $out 'run/server.log'))"
    $serverNewer = "python $(Quote-Arg $server) --feed $(Quote-Arg (Join-Path $out 'fixtures/newer.json')) --port $Port --log $(Quote-Arg (Join-Path $out 'run/server.log'))"
    # The diagnostic opener receives only the fixed release URL. The receipt
    # is a separate file so the browser result cannot be inferred from UI text.
    $receipt = Join-Path $out 'run/browser-receipt.txt'
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add('# Messages acceptance runbook')
    $lines.Add('')
    $lines.Add("Prepared from $exe (SHA256: $((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)); local version $version.")
    $lines.Add('Build with cmake --build --preset win-amd64-release before any live route. Rerun preparation with -Reuse after rebuilding, while all legs remain unmeasured.')
    $lines.Add('The owner drives each game route. Start server commands in a separate terminal and stop them with Ctrl+C.')
    $lines.Add('All launches use separate user roots except Feed, Icon and Cache, which intentionally share one cache.')
    $lines.Add('For each boot: reach PLAY, switch to MESSAGES with LT/RT, save the listed log and screenshots, then close the game.')
    $lines.Add('')
    $lines.Add('Current feed server:')
    $lines.Add('')
    $lines.Add('    ' + $serverCurrent)
    $lines.Add('')
    $lines.Add('Newer feed server:')
    $lines.Add('')
    $lines.Add('    ' + $serverNewer)
    $lines.Add('')
    $lines.Add('Before the UpdateOpen launch, set the receipt path in that PowerShell terminal:')
    $lines.Add('')
    $lines.Add('    $env:RBBLITZ_MESSAGES_BROWSER_RECEIPT = ' + (Quote-Arg $receipt))
    $lines.Add('')
    Add-RunbookLeg $lines 'Feed' 'Run the current feed server. Check both dated rows, select the second, and check its body. Log: messages: fetched; no fatal/native fault.' $launch.Feed
    Add-RunbookLeg $lines 'Icon' 'Keep the current server running. Capture each row selection; compare the two built-in icons by eye against the icon reference in plan section 9.1. If M3/M6 documents a fallback, record that expected result.' $launch.Icon
    Add-RunbookLeg $lines 'Cache' 'Stop the server. Reuse the online root. Check the same two rows and a cached-feed log line; a failed fetch must retain cached.' $launch.Cache
    Add-RunbookLeg $lines 'Bundled' 'Keep the server stopped. Fresh root: check the shipped welcome row and bundled-feed log line.' $launch.Bundled
    Add-RunbookLeg $lines 'NoValidFeed' 'Keep the server stopped. The isolated executable has no assets/messages/feed.json. Check retail No messages/apology, no START prompt, and the no-valid-feed log line.' $launch.NoValidFeed
    Add-RunbookLeg $lines 'UpdateLater' 'Run the newer server. Check UPDATE GAME, press START, check the confirmation, then press Later and check the inbox return.' $launch.UpdateLater
    Add-RunbookLeg $lines 'UpdateOpen' "Run the newer server. Press START then Open releases. Check $receipt contains only $releaseUrl and the game exits after opening." $launch.UpdateOpen
    Add-RunbookLeg $lines 'UpdateFailure' 'Run the newer server. Press START then Open releases with the deliberately missing diagnostic opener. Check the guest error and that the game remains open.' $launch.UpdateFailure
    Add-RunbookLeg $lines 'Current' 'Run the current server. Check no START prompt and that START does not navigate.' $launch.Current
    $lines.Add('## Pages')
    $lines.Add('')
    $lines.Add("When available, request $pagesUrl and record HTTP 200 plus validated JSON separately from local fixture results.")
    $lines.Add('')
    $lines.Add('Record each verdict using -Mode Record -Leg <name> -Verdict pass|fail -Evidence <paths> -Note <finding>.')
    Write-Utf8 (Join-Path $out 'runbook.md') ($lines -join [Environment]::NewLine)
    $matrix = [ordered]@{}
    foreach ($name in $legs) { $matrix[$name] = [ordered]@{ verdict = 'unmeasured'; evidence = @(); note = '' } }
    Save-Summary ([ordered]@{
        prepared_at_utc = [DateTime]::UtcNow.ToString('o')
        game_version = $version
        newer_version = $newer
        build_sha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
        bundled_sha256 = (Get-FileHash -LiteralPath $bundle -Algorithm SHA256).Hash
        matrix = $matrix
    })
    Write-Host "Prepared $out; all live and Pages legs remain unmeasured."
    Write-Host "Runbook: $(Join-Path $out 'runbook.md')"
    exit 0
}

if ($Mode -eq 'Record') {
    if (-not $Leg -or -not $Verdict) { throw '-Mode Record requires -Leg and -Verdict.' }
    $summary = Read-Summary
    $builtExe = Join-Path $build 'rb_blitz.exe'
    if (-not (Test-Path -LiteralPath $builtExe) -or
        (Get-FileHash -LiteralPath $builtExe -Algorithm SHA256).Hash -ne $summary.build_sha256) {
        throw 'The game build changed after preparation; use a new -OutDir for fresh evidence.'
    }
    $checked = @()
    foreach ($item in @($Evidence)) {
        $path = Resolve-UnderRoot $item
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Evidence file missing: $path" }
        $checked += $path
    }
    if ($Verdict -ne 'unmeasured' -and $checked.Count -eq 0) {
        throw 'A pass/fail verdict requires at least one existing evidence file.'
    }
    $summary.matrix.$Leg.verdict = $Verdict
    $summary.matrix.$Leg.evidence = $checked
    $summary.matrix.$Leg.note = $Note
    Save-Summary $summary
}

$summary = Read-Summary
foreach ($name in $legs) {
    $entry = $summary.matrix.$name
    Write-Host ("{0,-15} {1,-10} {2}" -f $name, $entry.verdict, $entry.note)
}
