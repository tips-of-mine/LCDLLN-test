# Génère lcdlln_client.ico et lcdlln_world_editor.ico (multi-résolution PNG embarqués).
# Usage : powershell -NoProfile -ExecutionPolicy Bypass -File tools/gen_exe_icons.ps1
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Draw-GameIcon([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $bg = [System.Drawing.Color]::FromArgb(255, 13, 15, 20)
    $g.Clear($bg)
    $gold = [System.Drawing.Color]::FromArgb(255, 201, 169, 110)
    $purple = [System.Drawing.Color]::FromArgb(255, 120, 80, 180)
    $cx = [float]($s / 2.0)
    $cy = [float]($s / 2.0)
    $r = [float]($s * 0.38)
    $brushMoon = New-Object System.Drawing.SolidBrush $gold
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path.AddEllipse($cx - $r, $cy - $r, 2.0 * $r, 2.0 * $r)
    $biteR = [float]($r * 0.92)
    $biteX = [float]($cx + $r * 0.42)
    $path2 = New-Object System.Drawing.Drawing2D.GraphicsPath
    $path2.AddEllipse($biteX - $biteR, $cy - $biteR, 2.0 * $biteR, 2.0 * $biteR)
    $region = New-Object System.Drawing.Region $path
    $region.Exclude($path2)
    $g.FillRegion($brushMoon, $region)
    $pw = [float][math]::Max(1.0, $s / 32.0)
    $penGold = New-Object System.Drawing.Pen -ArgumentList @($gold, $pw)
    $g.DrawEllipse($penGold, $cx - $r, $cy - $r, 2.0 * $r, 2.0 * $r)
    if ($s -ge 32) {
        $gp = New-Object System.Drawing.Drawing2D.GraphicsPath
        $w = [float]($s * 0.06)
        $gp.AddLine($cx - $w, $cy + $r * 0.15, $cx, $cy + $r * 0.55)
        $gp.AddLine($cx, $cy + $r * 0.55, $cx + $w * 1.2, $cy - $r * 0.35)
        $g.DrawPath($penGold, $gp)
    }
    $brP = New-Object System.Drawing.SolidBrush $purple
    $dot = [float][math]::Max(1.0, $s / 16.0)
    $g.FillEllipse($brP, ($cx - $r * 0.25 - $dot / 2.0), ($cy - $r * 0.55 - $dot / 2.0), $dot, $dot)
    $g.Dispose()
    return $bmp
}

function Draw-EditorIcon([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap $s, $s
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $bg = [System.Drawing.Color]::FromArgb(255, 16, 20, 28)
    $g.Clear($bg)
    $grid = [System.Drawing.Color]::FromArgb(200, 90, 140, 190)
    $accent = [System.Drawing.Color]::FromArgb(255, 201, 169, 110)
    $pwG = [float][math]::Max(1.0, $s / 64.0)
    $penG = New-Object System.Drawing.Pen -ArgumentList @($grid, $pwG)
    $step = [float]($s / 4.0)
    for ($i = 1; $i -lt 4; $i++) {
        $x = $step * $i
        $g.DrawLine($penG, $x, [float]($s * 0.12), $x, [float]($s * 0.88))
        $g.DrawLine($penG, [float]($s * 0.12), $x, [float]($s * 0.88), $x)
    }
    $m = [float]($s * 0.22)
    $pwA = [float][math]::Max(2.0, $s / 20.0)
    $penA = New-Object System.Drawing.Pen -ArgumentList @($accent, $pwA)
    $rx = [float]($s / 2.0 - $m)
    $ry = [float]($s / 2.0 - $m)
    $g.DrawRectangle($penA, $rx, $ry, 2.0 * $m, 2.0 * $m)
    $g.DrawLine($penA, [float]($s * 0.12), [float]($s * 0.12), [float]($s * 0.12 + $m * 0.9), [float]($s * 0.12))
    $g.DrawLine($penA, [float]($s * 0.12), [float]($s * 0.12), [float]($s * 0.12), [float]($s * 0.12 + $m * 0.9))
    $g.Dispose()
    return $bmp
}

function Write-PngIco([string]$outPath, [System.Drawing.Bitmap[]]$bitmaps) {
    $pngBytesList = New-Object System.Collections.Generic.List[byte[]]
    foreach ($b in $bitmaps) {
        $ms = New-Object System.IO.MemoryStream
        $b.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        [void]$pngBytesList.Add($ms.ToArray())
        $ms.Dispose()
    }
    $fs = [IO.File]::Create($outPath)
    $bw = New-Object IO.BinaryWriter $fs
    $bw.Write([uint16]0)
    $bw.Write([uint16]1)
    $bw.Write([uint16]$pngBytesList.Count)
    $offset = 6 + 16 * $pngBytesList.Count
    $idx = 0
    foreach ($b in $bitmaps) {
        $w = $b.Width
        $h = $b.Height
        $wb = if ($w -ge 256) { [byte]0 } else { [byte]$w }
        $hb = if ($h -ge 256) { [byte]0 } else { [byte]$h }
        $bw.Write($wb)
        $bw.Write($hb)
        $bw.Write([byte]0)
        $bw.Write([byte]0)
        $bw.Write([uint16]1)
        $bw.Write([uint16]32)
        $len = $pngBytesList[$idx].Length
        $bw.Write([uint32]$len)
        $bw.Write([uint32]$offset)
        $offset += $len
        $idx++
    }
    foreach ($pb in $pngBytesList) {
        $bw.Write($pb)
    }
    $bw.Close()
    $fs.Close()
}

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $repoRoot "CMakeLists.txt"))) {
    $walk = $PSScriptRoot
    while ($walk -and -not (Test-Path (Join-Path $walk "CMakeLists.txt"))) {
        $walk = Split-Path -Parent $walk
    }
    $repoRoot = $walk
}
$iconsDir = Join-Path $repoRoot "game/data/icons"
if (-not (Test-Path $iconsDir)) {
    New-Item -ItemType Directory -Path $iconsDir -Force | Out-Null
}

$sizes = @(16, 32, 48, 64, 256)
$gameBmps = foreach ($sz in $sizes) { Draw-GameIcon $sz }
$editBmps = foreach ($sz in $sizes) { Draw-EditorIcon $sz }
Write-PngIco (Join-Path $iconsDir "lcdlln_client.ico") @($gameBmps)
Write-PngIco (Join-Path $iconsDir "lcdlln_world_editor.ico") @($editBmps)
foreach ($x in $gameBmps) { $x.Dispose() }
foreach ($x in $editBmps) { $x.Dispose() }
Write-Host "Wrote lcdlln_client.ico and lcdlln_world_editor.ico -> $iconsDir"
