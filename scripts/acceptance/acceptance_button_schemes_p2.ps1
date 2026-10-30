<#
.SYNOPSIS
  Builds and launches P2's locally generated controller fixture for the owner's UI check.
.EXAMPLE
  .\scripts\acceptance\acceptance_button_schemes_p2.ps1 -Scheme one-glyph
  .\scripts\acceptance\acceptance_button_schemes_p2.ps1 -Scheme one-pad -Backend vulkan
#>
[CmdletBinding()]
param(
    [ValidateSet('retail', 'one-glyph', 'one-pad', 'test-markers')]
    [string]$Scheme = 'one-glyph',
    [ValidateSet('d3d12', 'vulkan')][string]$Backend = 'd3d12'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $repo 'out/build/win-amd64-release'
$evidence = Join-Path $repo 'out/button-schemes/p2'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
Push-Location $repo
try {
    cmake --build --preset win-amd64-release
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    if ($Scheme -ne 'retail') {
        python scripts/controllers/generate_controller_scheme_fixtures.py
        if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed.' }
        & scripts/controllers/controller_schemes.ps1 -Validate -Pack `
            -Scheme (Join-Path $evidence "fixtures/$Scheme")
        if ($LASTEXITCODE -ne 0) { throw 'Fixture validation or pack failed.' }
        # This intentionally invalid package is kept only in the build tree so
        # the boot log can prove that discovery reports rejection reasons.
        $broken = Join-Path $evidence 'fixtures/invalid-missing'
        $brokenTarget = Join-Path $build 'assets/controller/schemes/invalid-missing'
        New-Item -ItemType Directory -Force -Path $brokenTarget | Out-Null
        Copy-Item -LiteralPath (Join-Path $broken 'scheme.toml') -Destination $brokenTarget -Force
    }
    $emptyDlc = Join-Path $evidence 'empty-dlc'
    New-Item -ItemType Directory -Force -Path $emptyDlc | Out-Null
    $old = @{}
    foreach ($key in @('RB_BLITZ_SCHEME', 'RB_BLITZ_P1_TRACE',
            'RB_BLITZ_P1_BUTTON_HASH', 'RB_BLITZ_P1_PAD_HASH', 'RB_BLITZ_P1_SWITCH_FILE')) {
        $old[$key] = [Environment]::GetEnvironmentVariable($key, 'Process')
        [Environment]::SetEnvironmentVariable($key, $null, 'Process')
    }
    try {
        $env:RB_BLITZ_SCHEME = $Scheme
        Write-Host "P2 $Backend/${Scheme}: navigate the menu prompts and, for pads, Help & Options > Controls > Gameplay Mapping > Controller > Controller Presets. Close the game when done."
        Write-Host 'This script starts the window and saves the log; you control the game.'
        $launchArgs = @('--game_data_root=..\..\..\game', "--gpu_backend=$Backend",
            "--dlc_root=`"$emptyDlc`"", "--dlc_library=`"$emptyDlc`"")
        $started = Get-Date
        $process = Start-Process -FilePath (Join-Path $build 'rb_blitz.exe') `
            -ArgumentList $launchArgs -WorkingDirectory $build -WindowStyle Normal -PassThru -Wait
    } finally {
        foreach ($key in $old.Keys) {
            [Environment]::SetEnvironmentVariable($key, $old[$key], 'Process')
        }
    }
    $latest = Get-ChildItem -LiteralPath (Join-Path $build 'logs') -Filter '*.log' -File |
        Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-5) } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($latest) {
        $destination = Join-Path $evidence ("$Backend-$Scheme-" +
            $started.ToString('yyyyMMdd-HHmmss') + '.log')
        Copy-Item -LiteralPath $latest.FullName -Destination $destination
        Write-Host "Log: $destination"
        Select-String -LiteralPath $destination -Pattern 'controller_schemes:|texture_override:' |
            ForEach-Object { Write-Host $_.Line }
    } else {
        Write-Warning 'No new game log was found under the build logs directory.'
    }
    Write-Host "Game exit code: $($process.ExitCode)"
} finally {
    Pop-Location
}
