# Milestone 3 acceptance: 10 consecutive launches must reach the title screen /
# offline prompt and each must close cleanly (no forced kill).
#
# Usage:  .\scripts\acceptance_launches.ps1 [-Runs 10] [-BootWaitSec 28]
param(
    [int]$Runs = 10,
    [int]$BootWaitSec = 28,
    [string]$BuildDir = "out/build/win-amd64-release"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "$BuildDir/rb_blitz.exe"
$work = Join-Path $root $BuildDir
$gameRoot = Join-Path $root "game"
$capDir = Join-Path $root "out/m3-acceptance"
New-Item -ItemType Directory -Force -Path $capDir | Out-Null

if (-not (Test-Path $exe)) { throw "not found: $exe" }

$results = @()
for ($i = 1; $i -le $Runs; $i++) {
    Get-ChildItem (Join-Path $work "logs/*.log") -ErrorAction SilentlyContinue | Remove-Item -Force
    $p = Start-Process -FilePath $exe -ArgumentList "--game_data_root=$gameRoot" `
        -WorkingDirectory $work -PassThru
    Start-Sleep -Seconds $BootWaitSec

    $alive = -not $p.HasExited
    $log = Get-ChildItem (Join-Path $work "logs/*.log") -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $fatal = $false
    $logBytes = 0
    if ($log) {
        $logBytes = $log.Length
        $fatal = [bool](Select-String -Path $log.FullName -Pattern "\[FATAL\]" -Quiet)
    }

    $shot = Join-Path $capDir ("run{0:D2}.png" -f $i)
    if ($alive) {
        try { & (Join-Path $PSScriptRoot "capture_window.ps1") -OutFile $shot | Out-Null } catch {}
    }

    $closedCleanly = $false
    if (-not $p.HasExited) {
        $p.CloseMainWindow() | Out-Null
        if ($p.WaitForExit(15000)) { $closedCleanly = $true }
        else { Stop-Process -Id $p.Id -Force; $closedCleanly = $false }
    } else {
        $closedCleanly = $false  # exited on its own before we closed it
    }

    $results += [pscustomobject]@{
        Run        = $i
        AliveAtEnd = $alive
        Fatal      = $fatal
        LogKB      = [math]::Round($logBytes / 1KB)
        ClosedClean= $closedCleanly
        Shot       = if (Test-Path $shot) { "yes" } else { "-" }
    }
    Write-Host ("run {0,2}: alive={1} fatal={2} log={3}KB clean={4} shot={5}" -f `
        $i, $alive, $fatal, [math]::Round($logBytes/1KB), $closedCleanly, (Test-Path $shot))
}

Write-Host "`n=== summary ==="
$results | Format-Table -AutoSize
$pass = ($results | Where-Object { $_.AliveAtEnd -and -not $_.Fatal -and $_.ClosedClean }).Count
Write-Host "reached-title-and-closed-clean: $pass / $Runs"
