# Print a coarse luminance map of a screenshot so the bring-up loop can "see"
# frame layout (dialog box vs. full-screen scene, where text sits) without an
# image viewer. Pairs with ocr_image.ps1 for the text itself.
#
# Usage:
#   .\scripts\ascii_preview.ps1 -Path out\drive-ui\m4-02-now.png
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [int]$Cols = 96,
    [int]$Rows = 30
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$img = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Path).Path)
try {
    $chars = " .:-=+*#%@"
    $sw = [double]$img.Width / $Cols
    $sh = [double]$img.Height / $Rows
    for ($r = 0; $r -lt $Rows; $r++) {
        $line = New-Object Text.StringBuilder
        for ($c = 0; $c -lt $Cols; $c++) {
            $sum = 0; $n = 0
            for ($y = [int]($r * $sh); $y -lt [int](($r + 1) * $sh); $y += 2) {
                for ($x = [int]($c * $sw); $x -lt [int](($c + 1) * $sw); $x += 2) {
                    $p = $img.GetPixel($x, $y)
                    $sum += 0.299 * $p.R + 0.587 * $p.G + 0.114 * $p.B
                    $n++
                }
            }
            $v = if ($n) { $sum / $n } else { 0 }
            $idx = [int][Math]::Min(9, [Math]::Floor($v / 25.6))
            $line.Append($chars[$idx]) | Out-Null
        }
        $line.ToString()
    }
} finally {
    $img.Dispose()
}
