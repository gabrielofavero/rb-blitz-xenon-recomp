# SPDX-License-Identifier: GPL-2.0-only
# Export the accepted wheel icons from the authored middle-mouse PNG. The two
# arrows use the same pixels and placement as DrawWheelArrow's runtime fallback.
param(
    [string]$AssetDirectory = (Join-Path $PSScriptRoot '..\..\assets\controller\keyboard-mouse')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Set-ArrowPixel {
    param([System.Drawing.Bitmap]$Image, [int]$X, [int]$Y, [bool]$Shadow)
    if ($X -lt 0 -or $Y -lt 0 -or $X -ge $Image.Width -or $Y -ge $Image.Height) { return }
    $color = if ($Shadow) {
        [System.Drawing.Color]::FromArgb(255, 20, 22, 25)
    } else {
        [System.Drawing.Color]::FromArgb(255, 238, 241, 245)
    }
    $Image.SetPixel($X, $Y, $color)
}

function Draw-Arrow {
    param([System.Drawing.Bitmap]$Image, [bool]$Up)
    $cx = [int][Math]::Floor($Image.Width / 2)
    $top = [int][Math]::Floor($Image.Height * 43 / 100)
    $size = [Math]::Max(6, [int][Math]::Floor([Math]::Min($Image.Width, $Image.Height) / 7))
    foreach ($shadow in @($true, $false)) {
        for ($y = 0; $y -le 2 * $size; ++$y) {
            $half = if ($Up) {
                if ($y -lt $size) { $y } else { 2 }
            } else {
                if ($y -lt $size) { 2 } else { 2 * $size - $y }
            }
            for ($dx = -$half; $dx -le $half; ++$dx) {
                $offset = if ($shadow) { 1 } else { 0 }
                Set-ArrowPixel $Image ($cx + $dx + $offset) ($top + $y + $offset) $shadow
            }
        }
    }
}

$sourcePath = Join-Path $AssetDirectory 'mouse_middle.png'
$source = [System.Drawing.Bitmap]::new($sourcePath)
try {
    if ($source.Width -ne 68 -or $source.Height -ne 92) {
        throw "mouse_middle.png changed from its 68x92 authored canvas"
    }
    foreach ($direction in @('up', 'down')) {
        $image = [System.Drawing.Bitmap]::new($source.Width, $source.Height,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try {
            for ($y = 0; $y -lt $source.Height; ++$y) {
                for ($x = 0; $x -lt $source.Width; ++$x) {
                    $image.SetPixel($x, $y, $source.GetPixel($x, $y))
                }
            }
            Draw-Arrow $image ($direction -eq 'up')
            $destination = Join-Path $AssetDirectory "mouse_wheel_$direction.png"
            $image.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
            Write-Output $destination
        } finally {
            $image.Dispose()
        }
    }
} finally {
    $source.Dispose()
}
