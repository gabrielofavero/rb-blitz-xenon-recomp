# P2 controller package validator and build-tree packer.
param(
    [switch]$Validate,
    [switch]$Pack,
    [string]$Scheme,
    [string]$Destination
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$tool = Join-Path $repo 'out/build/win-amd64-release/rb_blitz_controller_schemes.exe'
if (-not (Test-Path -LiteralPath $tool)) {
    throw "Build rb_blitz_controller_schemes first: $tool"
}
if (-not $Validate -and -not $Pack) { $Validate = $true }
$source = Join-Path $repo 'assets/controller'
if ($Scheme) {
    $folders = @((Resolve-Path -LiteralPath $Scheme).Path)
} elseif (Test-Path -LiteralPath $source) {
    $folders = @(Get-ChildItem -LiteralPath $source -Directory |
        Where-Object { $_.Name -notlike 'template-*' -and
            (Test-Path -LiteralPath (Join-Path $_.FullName 'scheme.toml')) } |
        ForEach-Object FullName)
} else {
    $folders = @()
}
if (-not $Destination) {
    $Destination = Join-Path $repo 'out/build/win-amd64-release/assets/controller/schemes'
}
foreach ($folder in $folders) {
    $internal = (Split-Path -Leaf $folder) -eq 'keyboard-mouse'
    $mode = if ($internal) { '--validate-internal-keyboard' } else { '--validate' }
    $listMode = if ($internal) { '--list-internal-keyboard' } else { '--list-declared' }
    $report = @(& $tool $mode $folder)
    if ($LASTEXITCODE -ne 0) { throw "Invalid controller scheme: $folder" }
    if (-not $Pack) { $report | ForEach-Object { Write-Host $_ } }
    if ($Pack) {
        $targetRoot = if ($internal) { Split-Path -Parent $Destination } else { $Destination }
        New-Item -ItemType Directory -Path $targetRoot -Force | Out-Null
        $target = Join-Path $targetRoot (Split-Path -Leaf $folder)
        $destinationFull = [IO.Path]::GetFullPath($targetRoot).TrimEnd('\', '/')
        $targetFull = [IO.Path]::GetFullPath($target)
        if (-not $targetFull.StartsWith($destinationFull + [IO.Path]::DirectorySeparatorChar,
                                         [StringComparison]::OrdinalIgnoreCase)) {
            throw "Pack target escapes destination: $targetFull"
        }
        if (Test-Path -LiteralPath $targetFull) {
            Remove-Item -LiteralPath $targetFull -Recurse -Force
        }
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        $files = @(& $tool $listMode $folder)
        if ($LASTEXITCODE -ne 0) { throw "Cannot list declared files: $folder" }
        foreach ($relative in $files) {
            $from = Join-Path $folder $relative
            $to = Join-Path $target $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $to) -Force | Out-Null
            Copy-Item -LiteralPath $from -Destination $to -Force
        }
        Write-Host "Packed $folder -> $target"
    }
}
if ($folders.Count -eq 0) { Write-Host 'No owner controller schemes found.' }
