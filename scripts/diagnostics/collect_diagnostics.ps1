<#
.SYNOPSIS
Creates a local ZIP of stutter diagnostics, ready to share after an occurrence.
.DESCRIPTION
Run before updating the game. Close the game first for the most complete logs.
The default input is the standard Saved Games diagnostics folder; custom save
locations must be supplied with -DiagnosticsDirectory. No upload is performed.
#>
[CmdletBinding()]
param(
    [string]$DiagnosticsDirectory,
    [string]$GameDirectory = $PSScriptRoot,
    [string]$OutputPath,
    [string]$Note = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

if (-not $DiagnosticsDirectory) {
    $documents = [Environment]::GetFolderPath('MyDocuments')
    $DiagnosticsDirectory = Join-Path $documents 'Rock Band Blitz\Saved Games\diagnostics'
}
$inputDirectory = (Get-Item -LiteralPath $DiagnosticsDirectory).FullName
if (-not (Test-Path -LiteralPath $inputDirectory -PathType Container)) {
    throw "Not a diagnostics directory: $inputDirectory"
}
if (-not $OutputPath) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $OutputPath = Join-Path ([Environment]::GetFolderPath('Desktop')) "rb-blitz-diagnostics-$stamp.zip"
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
if (Test-Path -LiteralPath $OutputPath) { throw "Output already exists: $OutputPath" }
$temporary = $OutputPath + '.partial'

$report = [ordered]@{
    schema = 1
    collected_utc = [DateTime]::UtcNow.ToString('o')
    diagnostics_directory = $inputDirectory
    game_directory = [IO.Path]::GetFullPath($GameDirectory)
    user_note = $Note
    files = @()
    missing = @()
    skipped = @()
    build_files_at_collection = @()
    system_at_collection = [ordered]@{
        os = [Environment]::OSVersion.VersionString
        logical_processors = [Environment]::ProcessorCount
        video_controllers = @()
        video_query_error = $null
    }
    limits = @(
        'Only the current and preceding retained sessions are available; collect before another launch or update.'
        'Live files are copied from one open handle up to their initial length; the last record may be incomplete.'
        'Build hashes describe files at collection time; session-info.txt describes the launched build.'
        'Timing overlap alone does not establish a root cause; GPU/OS traces may still be needed.'
    )
}
try {
    $report.system_at_collection.video_controllers = @(Get-CimInstance Win32_VideoController |
        Select-Object Name, DriverVersion, DriverDate)
} catch {
    $report.system_at_collection.video_query_error = $_.Exception.Message
}
$names = @('game.log', 'game.1.log', 'previous.log', 'previous.1.log',
           'session-info.txt', 'previous-session-info.txt', 'unmapped-shaders.jsonl')
$total = 0L
$file = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try {
    $zip = [IO.Compression.ZipArchive]::new($file, [IO.Compression.ZipArchiveMode]::Create, $true)
    try {
        foreach ($name in $names) {
            $sourcePath = Join-Path $inputDirectory $name
            if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
                $report.missing += $name
                continue
            }
            $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
            $source = [IO.File]::Open($sourcePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
            try {
                $length = $source.Length
                if ($length -gt 64MB -or $total + $length -gt 128MB) {
                    $report.skipped += @{name = $name; bytes = $length; reason = 'capture size limit'}
                    continue
                }
                $entry = $zip.CreateEntry('diagnostics/' + $name)
                $destination = $entry.Open()
                $hash = [Security.Cryptography.SHA256]::Create()
                try {
                    $buffer = [byte[]]::new(65536)
                    $copied = 0L
                    while ($copied -lt $length) {
                        $count = $source.Read($buffer, 0, [int][Math]::Min($buffer.Length, $length - $copied))
                        if ($count -eq 0) { break }
                        $destination.Write($buffer, 0, $count)
                        [void]$hash.TransformBlock($buffer, 0, $count, $null, 0)
                        $copied += $count
                    }
                    [void]$hash.TransformFinalBlock([byte[]]::new(0), 0, 0)
                    $report.files += [ordered]@{
                        name = $name; source = $sourcePath; bytes = $copied
                        initial_bytes = $length; complete_snapshot = ($copied -eq $length)
                        sha256 = ([BitConverter]::ToString($hash.Hash)).Replace('-', '').ToLowerInvariant()
                    }
                    $total += $copied
                } finally { $destination.Dispose(); $hash.Dispose() }
            } finally { $source.Dispose() }
        }
        if ($report.files.Count -eq 0) { throw 'No recognized diagnostic files were found.' }
        foreach ($name in @('rb_blitz.exe', 'rexruntime.dll', 'rexgpu-xenos.dll', 'graphics-preparation.json')) {
            $path = Join-Path $GameDirectory $name
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
            $item = Get-Item -LiteralPath $path
            $report.build_files_at_collection += @{
                name = $name; bytes = $item.Length
                sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
        $manifest = $zip.CreateEntry('manifest.json').Open()
        try {
            $bytes = [Text.Encoding]::UTF8.GetBytes(($report | ConvertTo-Json -Depth 8))
            $manifest.Write($bytes, 0, $bytes.Length)
        } finally { $manifest.Dispose() }
    } finally { $zip.Dispose() }
} finally { $file.Dispose() }
[IO.File]::Move($temporary, $OutputPath)
Write-Output "Diagnostics copied from: $inputDirectory"
Write-Output "Share this file: $OutputPath"
if ($report.skipped.Count -gt 0) { Write-Warning 'Some files exceeded the capture limits; see manifest.json.' }
