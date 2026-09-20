# OCR a screenshot with the Windows.Media.Ocr engine and print the text.
#
# Why this exists: the bring-up loop verifies UI state (title screen, dialogs,
# menus) from `scripts\capture_window.ps1` captures. A human can read the PNG;
# an automated loop cannot, and the XEX is encrypted so the rendered text is not
# in the guest image either. Windows' built-in OCR is available on any Win10+
# box with no extra install and reads the game's UI font well enough to tell
# "PRESS START" from "Cannot connect to Rock Central".
#
# Usage:
#   .\scripts\ocr_image.ps1 -Path out\drive-ui\m4-title.png
#   .\scripts\ocr_image.ps1 -Path shot.png -GrayscaleScale 3
#   .\scripts\ocr_image.ps1 -Path shot.png -Crop 0,400,1097,217
param(
    [Parameter(Mandatory = $true)][string]$Path,
    # Upscale factor before OCR; small UI text recognises more reliably at 2-3x.
    [int]$GrayscaleScale = 2,
    # Optional "x,y,w,h" sub-rectangle. Isolating one text band (a dialog, a
    # menu column) recognises better than feeding the whole frame.
    [string]$Crop
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Runtime.WindowsRuntime
Add-Type -AssemblyName System.Runtime.InteropServices.WindowsRuntime
Add-Type -AssemblyName System.Drawing

function Await($WinRtTask, $ResultType) {
    $asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
            $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
        })[0].MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
}

[Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime] | Out-Null
[Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime] | Out-Null
[Windows.Media.Ocr.OcrEngine, Windows.Media.Ocr, ContentType = WindowsRuntime] | Out-Null

$resolved = (Resolve-Path -LiteralPath $Path).Path

$cropRect = $null
if ($Crop) {
    $p = $Crop.Split(",") | ForEach-Object { [int]$_ }
    if ($p.Count -ne 4) { throw "-Crop must be 'x,y,w,h'" }
    $cropRect = New-Object System.Drawing.Rectangle $p[0], $p[1], $p[2], $p[3]
}

# OCR works on the decoder's software bitmap. Crop to the requested band and
# scale it up so the game's small UI glyphs fall into the engine's preferred
# height; a full frame of background art otherwise drowns the text out.
$ras = $null
if ($GrayscaleScale -ne 1 -or $cropRect) {
    $src = [System.Drawing.Image]::FromFile($resolved)
    $x = if ($cropRect) { $cropRect.X } else { 0 }
    $y = if ($cropRect) { $cropRect.Y } else { 0 }
    $cw = if ($cropRect) { $cropRect.Width } else { $src.Width }
    $ch = if ($cropRect) { $cropRect.Height } else { $src.Height }
    $w = [int]($cw * $GrayscaleScale)
    $h = [int]($ch * $GrayscaleScale)
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $srcRect = New-Object System.Drawing.Rectangle $x, $y, $cw, $ch
    $dstRect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $g.DrawImage($src, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose(); $src.Dispose()
    # Hand the prepared bitmap to WinRT through memory; writing a temp file
    # makes it easy to OCR a stale copy of a previous run by accident.
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $ms.Position = 0
    $ras = [System.IO.WindowsRuntimeStreamExtensions]::AsRandomAccessStream($ms)
}

if ($ras) {
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($ras)) ([Windows.Graphics.Imaging.BitmapDecoder])
} else {
    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($resolved)) ([Windows.Storage.StorageFile])
    $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
}
$bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
if (-not $engine) { throw "no OCR engine for the user profile languages" }
$result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])

foreach ($line in $result.Lines) {
    Write-Output $line.Text
}
if ($ras) { $ras.Dispose() }
