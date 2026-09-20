# Compare captured game-window frames to decide whether the guest is still
# presenting new content and how much of the screen moves. Used during menu
# bring-up where the interesting question is "did that input do anything?".
#
# Usage: .\scripts\frame_diff.ps1 -Files out\drive-ui\a.png,out\drive-ui\b.png
param(
    [Parameter(Mandatory = $true)][string[]]$Files,
    [int]$Tolerance = 8
)

Add-Type -AssemblyName System.Drawing

# `powershell -File` passes "a.png,b.png" as a single argument, so accept both
# an array and a comma separated string.
$Files = @($Files | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

function Get-Pixels([string]$path) {
    $full = (Resolve-Path -LiteralPath $path).Path
    $src = [System.Drawing.Bitmap]::FromFile($full)
    $bmp = New-Object System.Drawing.Bitmap $src.Width, $src.Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.DrawImageUnscaled($src, 0, 0)
    $g.Dispose()
    $src.Dispose()
    $rect = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($data.Stride * $bmp.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($data)
    $result = [pscustomobject]@{ Bytes = $bytes; Width = $bmp.Width; Height = $bmp.Height; Stride = $data.Stride }
    $bmp.Dispose()
    return $result
}

$frames = @()
foreach ($f in $Files) { $frames += Get-Pixels $f }

for ($i = 0; $i -lt $frames.Count - 1; $i++) {
    $a = $frames[$i]
    $b = $frames[$i + 1]
    if ($a.Width -ne $b.Width -or $a.Height -ne $b.Height) {
        Write-Output "$($Files[$i]) vs $($Files[$i+1]): SIZE MISMATCH $($a.Width)x$($a.Height) vs $($b.Width)x$($b.Height)"
        continue
    }
    $changed = 0
    $maxDelta = 0
    $sumDelta = [int64]0
    $minX = $a.Width
    $minY = $a.Height
    $maxX = -1
    $maxY = -1
    for ($y = 0; $y -lt $a.Height; $y++) {
        $rowA = $y * $a.Stride
        $rowB = $y * $b.Stride
        for ($x = 0; $x -lt $a.Width; $x++) {
            $oA = $rowA + $x * 4
            $oB = $rowB + $x * 4
            $d = [Math]::Abs($a.Bytes[$oA] - $b.Bytes[$oB])
            $d1 = [Math]::Abs($a.Bytes[$oA + 1] - $b.Bytes[$oB + 1])
            $d2 = [Math]::Abs($a.Bytes[$oA + 2] - $b.Bytes[$oB + 2])
            if ($d1 -gt $d) { $d = $d1 }
            if ($d2 -gt $d) { $d = $d2 }
            if ($d -gt 0) {
                $sumDelta += $d
                if ($d -gt $maxDelta) { $maxDelta = $d }
            }
            if ($d -gt $Tolerance) {
                $changed++
                if ($x -lt $minX) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if ($y -lt $minY) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
    }
    $total = $a.Width * $a.Height
    $pct = [Math]::Round(100.0 * $changed / $total, 3)
    $mean = [Math]::Round($sumDelta / [double]$total, 4)
    $bbox = if ($maxX -ge 0) { "bbox $minX,$minY - $maxX,$maxY" } else { "no change" }
    Write-Output ("{0} -> {1}: {2}% pixels differ (tol>{3}), maxDelta={4}, meanDelta={5}, {6}" -f `
            (Split-Path -Leaf $Files[$i]), (Split-Path -Leaf $Files[$i + 1]), $pct, $Tolerance, $maxDelta, $mean, $bbox)
}
