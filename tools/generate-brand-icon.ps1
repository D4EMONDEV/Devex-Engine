# Rebuild the checked-in Windows icon from the original logo; no build-time imaging dependency.
param(
    [string]$Source = (Join-Path $PSScriptRoot '../engine/resources/icons/devex.png'),
    [string]$Destination = (Join-Path $PSScriptRoot '../engine/resources/icons/devex.ico'),
    [string]$PngDestination
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$logo = [System.Drawing.Image]::FromFile([IO.Path]::GetFullPath($Source))
$frames = [Collections.Generic.List[byte[]]]::new()
$sizes = @(16, 24, 32, 48, 64, 128, 256)
try {
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $stream = [IO.MemoryStream]::new()
        try {
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $scale = [Math]::Min($size / $logo.Width, $size / $logo.Height)
            $width = [int][Math]::Round($logo.Width * $scale)
            $height = [int][Math]::Round($logo.Height * $scale)
            $rectangle = [System.Drawing.Rectangle]::new([int][Math]::Floor(($size - $width) / 2), [int][Math]::Floor(($size - $height) / 2), $width, $height)
            $graphics.DrawImage($logo, $rectangle)
            $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        } finally {
            $stream.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally {
    $logo.Dispose()
}
$output = [IO.File]::Create([IO.Path]::GetFullPath($Destination))
$writer = [IO.BinaryWriter]::new($output)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; ++$index) {
        $side = [byte]($sizes[$index] % 256)
        $writer.Write($side)
        $writer.Write($side)
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$frames[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $frames[$index].Length
    }
    foreach ($frame in $frames) { $writer.Write($frame) }
} finally {
    $writer.Dispose()
}
if ($PngDestination) {
    [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($PngDestination), $frames[$frames.Count - 1])
}
