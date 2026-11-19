# Headless Windows development helpers: no game process or input automation.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../scripts/dev/dev-windows.ps1')
$repository = $Global:RbBlitzRoot
$scratch = Join-Path $repository ('out/dev-command-tests/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $scratch | Out-Null
$originalLocal = $env:LOCALAPPDATA
$originalAppData = $env:APPDATA
$originalProfile = $env:RBBLITZ_CONTROLLER_SETTINGS_PROFILE
$dlcEnvironment = @{}
foreach ($name in @('REX_DLC_ROOT', 'REX_DLC_LIBRARY', 'REX_BLITZ_RUNTIME_DLC_ROOT', 'REX_BLITZ_RUNTIME_DLC_LIBRARY')) {
    $dlcEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
    [Environment]::SetEnvironmentVariable($name, $null)
}
try {
    $Global:RbBlitzRoot = $scratch
    $env:LOCALAPPDATA = $scratch
    $env:APPDATA = $scratch
    $env:RBBLITZ_CONTROLLER_SETTINGS_PROFILE = $null
    $install = Join-Path $scratch 'Programs/Rock Band Blitz'
    $library = Join-Path $install 'music with spaces'
    New-Item -ItemType Directory -Force -Path $library | Out-Null
    # Isolate registry discovery from whichever versions are installed on the host.
    function Get-ItemProperty { param($LiteralPath, $ErrorAction) return $null }
    [IO.File]::WriteAllText((Join-Path $install 'paths.toml'), "schema_version = 1`n[paths]`ndlc_root = 'music with spaces'`n")
    $result = @(Get-BlitzDlcArguments)
    if ($result.Count -ne 1 -or $result[0] -ne "--dlc_root=$library") { throw 'Installed relative DLC root was not inherited.' }
    foreach ($flag in @('--dlc_root=empty', '--blitz.runtime.dlc_library=empty')) {
        if (@(Get-BlitzDlcArguments -Extra $flag).Count) { throw "Explicit override was ignored: $flag" }
    }
    $env:REX_BLITZ_RUNTIME_DLC_ROOT = 'explicit'
    if (@(Get-BlitzDlcArguments).Count) { throw 'Canonical environment override was ignored.' }
    $env:REX_BLITZ_RUNTIME_DLC_ROOT = $null
    $build = Join-Path $scratch ('out/build/' + $Global:RbBlitzPreset)
    New-Item -ItemType Directory -Force -Path $build | Out-Null
    [IO.File]::WriteAllText((Join-Path $build 'paths.toml'), "schema_version = 1`n[paths]`ndlc_root = '$library'`n")
    if (@(Get-BlitzDlcArguments).Count) { throw 'Checkout DLC precedence was lost.' }

    $profile = Join-Path $scratch 'custom.toml'
    $profileText = "schema_version = 1`n[launch]`ntarget = 'ultimate'`n"
    $settings = Join-Path $scratch 'custom.game-settings.toml'
    $settingsText = "schema_version = 1`n# retain this comment`n[graphics]`naspect_ratio = 'auto'`nforce_16_by_9 = false`nrenderer = 'vulkan'`n[controller]`nscheme = 'xbox-series'`n"
    [IO.File]::WriteAllText($profile, $profileText)
    [IO.File]::WriteAllText($settings, $settingsText)
    $prepared = @(New-BlitzMobileArguments -Extra "--blitz.runtime.launcher_profile=$profile", '--log.file_name=space ; $literal')
    if ($prepared.Count -ne 2 -or $prepared[1] -ne '--log.file_name=space ; $literal') { throw 'Portrait argument forwarding changed.' }
    $copy = $prepared[0].Substring('--launcher_profile='.Length)
    $verify = @'
import pathlib, sys, tomllib
file = pathlib.Path(sys.argv[1])
doc = tomllib.loads(file.with_name('settings.toml').read_text())
assert doc['graphics']['interface_orientation'] == 'portrait'
assert doc['graphics']['gameplay_orientation'] == 'portrait'
assert doc['graphics']['aspect_ratio'] == 'auto'
assert doc['graphics']['force_16_by_9'] is False
assert doc['graphics']['renderer'] == 'vulkan'
assert doc['controller']['scheme'] == 'xbox-series'
assert doc['provenance']['graphics.interface_orientation'] == 'manual'
assert '# retain this comment' in file.with_name('settings.toml').read_text()
'@
    & python -c $verify $copy
    if ($LASTEXITCODE -ne 0) { throw 'Portrait TOML verification failed.' }
    if ([IO.File]::ReadAllText($profile) -cne $profileText -or [IO.File]::ReadAllText($settings) -cne $settingsText) {
        throw 'Portrait setup modified the source settings.'
    }
    # Exercise launch-tag handling through the quick path with its launcher mocked.
    function Start-BlitzQuick { $script:launchArgs = @($args) }
    Run-Blitz '--quick' '--mobile' "--launcher_profile=$profile" '--blitz.diagnostics.mouse_as_touch=false'
    if ('--blitz.diagnostics.mobile_preview=true' -notin $script:launchArgs -or '--mobile' -in $script:launchArgs) {
        throw 'Mobile launch tag did not enable the mobile settings policy.'
    }
    $touchDefault = [Array]::IndexOf($script:launchArgs, '--blitz.diagnostics.mouse_as_touch=true')
    $touchOverride = [Array]::IndexOf($script:launchArgs, '--blitz.diagnostics.mouse_as_touch=false')
    if ($touchDefault -lt 0 -or $touchOverride -le $touchDefault -or '--touch' -in $script:launchArgs) {
        throw 'Touch launch tag was not translated before explicit overrides.'
    }
    Run-Blitz '--quick'
    if (@($script:launchArgs | Where-Object { $_ -like '*mouse_as_touch*' }).Count) {
        throw 'Ordinary launches enabled touch emulation.'
    }
    Write-Output 'PASS: DLC precedence, portrait source preservation, argument forwarding, touch launch tag and override.'
} finally {
    $Global:RbBlitzRoot = $repository
    $env:LOCALAPPDATA = $originalLocal
    $env:APPDATA = $originalAppData
    $env:RBBLITZ_CONTROLLER_SETTINGS_PROFILE = $originalProfile
    foreach ($name in $dlcEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $dlcEnvironment[$name]) }
}
