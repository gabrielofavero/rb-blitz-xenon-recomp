<#
.SYNOPSIS
  Builds and launches the P4 last-device acceptance run. The person running it
  navigates the game; this script only collects the resulting log.
.EXAMPLE
  .\scripts\acceptance\acceptance_button_schemes_p4.ps1 -Backend d3d12
#>
[CmdletBinding()]
param([ValidateSet('d3d12', 'vulkan')][string]$Backend = 'd3d12')

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$build = Join-Path $repo 'out/build/win-amd64-release'
$evidence = Join-Path $repo 'out/button-schemes/p4'
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
Push-Location $repo
try {
    cmake --build --preset win-amd64-release
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
    $emptyDlc = Join-Path $evidence 'empty-dlc'
    New-Item -ItemType Directory -Force -Path $emptyDlc | Out-Null
    Write-Host 'P4: enter Help & Options > Controls, select a controller layout, then use a real key/mouse and a physical pad in alternating order.'
    Write-Host 'Check that the selected layout persists and the controller pad art stays selected. Close the game to collect the log.'
    $launchArgs = @('--game_data_root=..\..\..\game', "--gpu_backend=$Backend",
        "--dlc_root=`"$emptyDlc`"", "--dlc_library=`"$emptyDlc`"")
    $started = Get-Date
    $process = Start-Process -FilePath (Join-Path $build 'rb_blitz.exe') `
        -ArgumentList $launchArgs -WorkingDirectory $build -WindowStyle Normal -PassThru -Wait
    $latest = Get-ChildItem -LiteralPath (Join-Path $build 'logs') -Filter '*.log' -File |
        Where-Object { $_.LastWriteTime -ge $started.AddSeconds(-5) } |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw 'No new game log was found under the build logs directory.' }
    $destination = Join-Path $evidence ("$Backend-" +
        $started.ToString('yyyyMMdd-HHmmss') + '.log')
    Copy-Item -LiteralPath $latest.FullName -Destination $destination
    Write-Host "Log: $destination"
    Select-String -LiteralPath $destination -Pattern 'button_schemes: active|controller_schemes: selected' |
        ForEach-Object { Write-Host $_.Line }
    Write-Host "Game exit code: $($process.ExitCode)"
} finally {
    Pop-Location
}
