# P2 controller package validator and build-tree packer.
param(
    [switch]$Validate,
    [switch]$Pack,
    [string]$Scheme,
    [string]$Destination
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$tool = Join-Path $repo 'out/build/win-amd64-release/rb_blitz_controller_schemes.exe'
if (-not (Test-Path -LiteralPath $tool)) {
    throw "Build rb_blitz_controller_schemes first: $tool"
}
if (-not $Validate -and -not $Pack) { $Validate = $true }
$source = Join-Path $repo 'assets/controller/schemes'
if ($Scheme) {
    $folders = @((Resolve-Path -LiteralPath $Scheme).Path)
} elseif (Test-Path -LiteralPath $source) {
    $folders = @(Get-ChildItem -LiteralPath $source -Directory | ForEach-Object FullName)
} else {
    $folders = @()
}
if (-not $Destination) {
    $Destination = Join-Path $repo 'out/build/win-amd64-release/assets/controller/schemes'
}
foreach ($folder in $folders) {
    $report = @(& $tool --validate $folder)
    if ($LASTEXITCODE -ne 0) { throw "Invalid controller scheme: $folder" }
    $report | ForEach-Object { Write-Host $_ }
    if ($Pack) {
        if (@($report | Where-Object { $_ -match '^\s+ignored extra file:' }).Count) {
            throw "Remove undeclared files before packing: $folder"
        }
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        $target = Join-Path $Destination (Split-Path -Leaf $folder)
        $destinationFull = [IO.Path]::GetFullPath($Destination).TrimEnd('\', '/')
        $targetFull = [IO.Path]::GetFullPath($target)
        if (-not $targetFull.StartsWith($destinationFull + [IO.Path]::DirectorySeparatorChar,
                                         [StringComparison]::OrdinalIgnoreCase)) {
            throw "Pack target escapes destination: $targetFull"
        }
        if (Test-Path -LiteralPath $targetFull) {
            Remove-Item -LiteralPath $targetFull -Recurse -Force
        }
        New-Item -ItemType Directory -Path $target -Force | Out-Null
        Get-ChildItem -LiteralPath $folder -Force |
            Copy-Item -Destination $target -Recurse -Force
        Write-Host "Packed $folder -> $target"
    }
}
if ($folders.Count -eq 0) { Write-Host 'No owner controller schemes found.' }
