<#
.SYNOPSIS
Builds the optional Inno Setup wizard images from the side artwork.

.DESCRIPTION
The setup wizard can show a large image on the left of the welcome page and a
small square badge in the top-right corner. Both are optional: setup.iss only
uses them when they exist, and the artwork below is not ours, so the images are
not committed to the repository (installer/assets/*.bmp is ignored). Running this
script is therefore a deliberate, local act - and a failure here is not a build
failure (build.ps1 continues with no images).

Inno picks, for each display scaling, the image that best matches the area it has
to fill: the area is 202x386 logical units for the large image and 58x58 for the
small one at 100%, and grows with the user's DPI setting. So instead of one image
that has to be stretched on every machine, this script writes the whole ladder of
documented sizes and lets Setup choose. That keeps the artwork at (very near)
native resolution on a 100% display and avoids the blur of a 2.5x stretch on a
250% one.

The source is a 2:3 thumbnail; the wizard area is 164:314, which is taller and
narrower. The image is therefore centre-cropped rather than distorted, and the
square badge is a centre crop of that.

.PARAMETER Url
Artwork to download. Defaults to the SteamGridDB thumbnail the project uses. The
image is only ever fetched here, at build time, on the machine of whoever builds
the installer - it is never redistributed.

.PARAMETER SourceImage
Use this image file instead of downloading. Accepts .jpg, .png and .bmp.

.PARAMETER OutDir
Where to write the bitmaps. Defaults to installer\assets, which is where
setup.iss looks.

.PARAMETER Force
Overwrite the images even if they are newer than the source. By default an
existing, up-to-date ladder is left alone so build.ps1 does not redownload the
artwork on every run.

.EXAMPLE
powershell -ExecutionPolicy Bypass -NoProfile -File installer\tools\make_art.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -NoProfile -File installer\tools\make_art.ps1 -SourceImage D:\art\rbblitz-cover.png -Force
#>
[CmdletBinding()]
param(
    [string] $Url = 'https://cdn2.steamgriddb.com/thumb/76bedfaa27870523301332d13b10c5d1.jpg',
    [string] $SourceImage,
    [string] $OutDir,
    [switch] $Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$installerDir = Split-Path -Parent $PSScriptRoot

if (-not $OutDir) { $OutDir = Join-Path $installerDir 'assets' }

# [Setup] WizardImageFile: the image area is 202x386 at 100% and grows with the
# DPI setting; WizardSmallImageFile is the square in the corner, 58x58 at 100%.
# Inno Setup 6.6 documentation, "WizardImageFile" and "WizardSmallImageFile".
$largeSizes = @(
    @{ Width = 202; Height = 386 },
    @{ Width = 269; Height = 515 },
    @{ Width = 336; Height = 643 },
    @{ Width = 403; Height = 772 },
    @{ Width = 430; Height = 824 },
    @{ Width = 498; Height = 953 },
    @{ Width = 534; Height = 1022 }
)
$smallSizes = @(58, 77, 97, 116, 124, 143, 159)

function Get-LargeName {
    param([int] $Width, [int] $Height)
    return "wizard-large-${Width}x${Height}.bmp"
}

function Get-SmallName {
    param([int] $Size)
    return "wizard-small-${Size}x${Size}.bmp"
}

# The bitmaps are 24bpp on purpose: Inno Setup only applies alpha blending to the
# wizard images when WizardImageAlphaFormat says so, and a plain BMP with no
# alpha channel is the one format that cannot be misread.
function Export-Bitmap {
    param(
        [System.Drawing.Bitmap] $Source,
        [System.Drawing.Rectangle] $Crop,
        [int] $Width,
        [int] $Height,
        [string] $Path
    )

    $target = [System.Drawing.Bitmap]::new($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($target)
        try {
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $destination = [System.Drawing.Rectangle]::new(0, 0, $Width, $Height)
            $graphics.DrawImage($Source, $destination, $Crop, [System.Drawing.GraphicsUnit]::Pixel)
        } finally {
            $graphics.Dispose()
        }
        $target.Save($Path, [System.Drawing.Imaging.ImageFormat]::Bmp)
    } finally {
        $target.Dispose()
    }
}

Add-Type -AssemblyName System.Drawing

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$expected = @()
foreach ($size in $largeSizes) { $expected += (Join-Path $OutDir (Get-LargeName -Width $size.Width -Height $size.Height)) }
foreach ($size in $smallSizes) { $expected += (Join-Path $OutDir (Get-SmallName -Size $size)) }

$sourcePath = $SourceImage
$downloaded = $false

if (-not $sourcePath) {
    $missing = @($expected | Where-Object { -not (Test-Path -LiteralPath $_) })
    $upToDate = $missing.Count -eq 0 -and $expected.Count -gt 0 -and -not $Force
    if ($upToDate) {
        Write-Host "wizard images are already in $OutDir (pass -Force to rebuild them)"
        exit 0
    }

    $sourcePath = Join-Path ([System.IO.Path]::GetTempPath()) 'rbblitz-wizard-art.jpg'
    Write-Host "downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $sourcePath -UseBasicParsing
    $downloaded = $true
} elseif (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "source image not found: $sourcePath"
}

$bitmap = $null
try {
    $bitmap = [System.Drawing.Bitmap]::new($sourcePath)
    Write-Host ("source      : {0}x{1}" -f $bitmap.Width, $bitmap.Height)

    $targetAspect = $largeSizes[0].Width / $largeSizes[0].Height

    # Centre crop to the wizard area's aspect ratio, then scale to each size. The
    # crop is at most as wide as the source and at most as tall, so it never
    # invents pixels outside the artwork.
    $cropWidth = [int][Math]::Floor($bitmap.Height * $targetAspect)
    $cropHeight = $bitmap.Height
    if ($cropWidth -gt $bitmap.Width) {
        $cropWidth = $bitmap.Width
        $cropHeight = [int][Math]::Floor($bitmap.Width / $targetAspect)
    }
    $largeCrop = [System.Drawing.Rectangle]::new(
        [int][Math]::Floor(($bitmap.Width - $cropWidth) / 2),
        [int][Math]::Floor(($bitmap.Height - $cropHeight) / 2),
        $cropWidth, $cropHeight)

    $squareSide = [Math]::Min($largeCrop.Width, $largeCrop.Height)
    $smallCrop = [System.Drawing.Rectangle]::new(
        $largeCrop.X + [int][Math]::Floor(($largeCrop.Width - $squareSide) / 2),
        $largeCrop.Y + [int][Math]::Floor(($largeCrop.Height - $squareSide) / 2),
        $squareSide, $squareSide)

    Write-Host ("large crop  : {0}x{1} at {2},{3}" -f $largeCrop.Width, $largeCrop.Height, $largeCrop.X, $largeCrop.Y)
    Write-Host ("small crop  : {0}x{1} at {2},{3}" -f $smallCrop.Width, $smallCrop.Height, $smallCrop.X, $smallCrop.Y)

    foreach ($size in $largeSizes) {
        $path = Join-Path $OutDir (Get-LargeName -Width $size.Width -Height $size.Height)
        Export-Bitmap -Source $bitmap -Crop $largeCrop -Width $size.Width -Height $size.Height -Path $path
        Write-Host ("   large    : {0}" -f (Split-Path -Leaf $path))
    }
    foreach ($size in $smallSizes) {
        $path = Join-Path $OutDir (Get-SmallName -Size $size)
        Export-Bitmap -Source $bitmap -Crop $smallCrop -Width $size -Height $size -Path $path
        Write-Host ("   small    : {0}" -f (Split-Path -Leaf $path))
    }
} finally {
    if ($bitmap) { $bitmap.Dispose() }
    if ($downloaded -and (Test-Path -LiteralPath $sourcePath)) { Remove-Item -LiteralPath $sourcePath -Force }
}

Write-Host ''
Write-Host ("Wrote {0} wizard images to {1}" -f $expected.Count, $OutDir) -ForegroundColor Green
Write-Host 'The artwork is not ours: these files are ignored by git and belong to the release asset only.' -ForegroundColor Yellow
