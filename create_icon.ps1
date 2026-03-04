# Generates icon.ico (multi-size: 16, 32, 48, 256 px) — orange HDR design
# matching CreateHDRIcon() in hdrautostart.cpp exactly.
# Called automatically by build.bat
Add-Type -AssemblyName System.Drawing

function New-HdrBitmap($sz) {
    $bmp = New-Object System.Drawing.Bitmap $sz, $sz
    $g   = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit

    # Black background
    $g.Clear([System.Drawing.Color]::Black)

    # Auto-fit: find largest font (in PIXELS) where "HDR" fits on one row
    $sf = New-Object System.Drawing.StringFormat
    $sf.Alignment     = [System.Drawing.StringAlignment]::Center
    $sf.LineAlignment = [System.Drawing.StringAlignment]::Center
    $sf.FormatFlags   = [System.Drawing.StringFormatFlags]::NoWrap

    $font = $null
    for ($fh = $sz; $fh -ge 4; $fh--) {
        if ($font) { $font.Dispose() }
        # GraphicsUnit.Pixel = 2  — specifies size in pixels, not points
        $font = New-Object System.Drawing.Font("Arial", $fh, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
        $measured = $g.MeasureString("HDR", $font, [int]($sz * 2), $sf)
        if ($measured.Width -le ($sz - 2)) { break }
    }

    $rect = New-Object System.Drawing.RectangleF(0, 0, $sz, $sz)
    $g.DrawString("HDR", $font, [System.Drawing.Brushes]::White, $rect, $sf)

    $font.Dispose(); $sf.Dispose(); $g.Dispose()
    return $bmp
}

# Generate PNG bytes for each size
$sizes   = @(16, 32, 48, 256)
$pngData = @()
foreach ($sz in $sizes) {
    $bmp = New-HdrBitmap $sz
    $ms  = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngData += , $ms.ToArray()
    $ms.Dispose(); $bmp.Dispose()
}

# Write ICO file manually (PNG-in-ICO, supported on Windows Vista+)
$outPath = Join-Path $PSScriptRoot "icon.ico"
$fs = [System.IO.File]::OpenWrite($outPath)
$w  = New-Object System.IO.BinaryWriter($fs)

$count      = $sizes.Count
$dataOffset = 6 + $count * 16

# ICO header
$w.Write([uint16]0)       # reserved
$w.Write([uint16]1)       # type: 1 = icon
$w.Write([uint16]$count)  # number of images

# Directory entries
$offset = $dataOffset
for ($i = 0; $i -lt $count; $i++) {
    $sz   = $sizes[$i]
    $data = $pngData[$i]
    $dim  = if ($sz -eq 256) { [byte]0 } else { [byte]$sz }  # 0 means 256 in ICO
    $w.Write([byte]$dim)        # width
    $w.Write([byte]$dim)        # height
    $w.Write([byte]0)           # color count (0 = truecolor)
    $w.Write([byte]0)           # reserved
    $w.Write([uint16]1)         # color planes
    $w.Write([uint16]32)        # bits per pixel
    $w.Write([uint32]$data.Length)
    $w.Write([uint32]$offset)
    $offset += $data.Length
}

# Image data (PNG blobs)
foreach ($data in $pngData) { $w.Write($data) }

$w.Close(); $fs.Close()
Write-Host "icon.ico created: $outPath ($count sizes: $($sizes -join ', ')px)"
