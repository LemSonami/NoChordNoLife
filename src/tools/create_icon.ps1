param([Parameter(Mandatory=$true)][string]$Source,[Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$sourceImage=[Drawing.Image]::FromFile([IO.Path]::GetFullPath($Source))
$frames=New-Object 'Collections.Generic.List[byte[]]'
$sizes=@(16,24,32,48,64,128,256)
try {
    foreach($size in $sizes) {
        $bitmap=New-Object Drawing.Bitmap($size,$size,[Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics=[Drawing.Graphics]::FromImage($bitmap)
        $stream=New-Object IO.MemoryStream
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.CompositingMode=[Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
            $graphics.PixelOffsetMode=[Drawing.Drawing2D.PixelOffsetMode]::Half
            $side=[Math]::Min($sourceImage.Width,$sourceImage.Height)
            $graphics.DrawImage($sourceImage,(New-Object Drawing.Rectangle(0,0,$size,$size)),
                [single](($sourceImage.Width-$side)/2),[single](($sourceImage.Height-$side)/2),
                [single]$side,[single]$side,[Drawing.GraphicsUnit]::Pixel)
            $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        } finally { $stream.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
    }
    $file=[IO.File]::Create([IO.Path]::GetFullPath($Output))
    $writer=New-Object IO.BinaryWriter($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset=6+16*$sizes.Count
        for($index=0;$index -lt $sizes.Count;$index++) {
            $dimension=if($sizes[$index] -eq 256) { 0 } else { $sizes[$index] }
            $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
            $writer.Write([byte]0); $writer.Write([byte]0)
            $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$index].Length); $writer.Write([uint32]$offset)
            $offset+=$frames[$index].Length
        }
        foreach($frame in $frames) { $writer.Write([byte[]]$frame) }
    } finally { $writer.Dispose(); $file.Dispose() }
} finally { $sourceImage.Dispose() }
