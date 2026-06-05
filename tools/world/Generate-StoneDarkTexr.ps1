<#
.SYNOPSIS
    Genere la texture albedo de pierre taillee sombre stone_dark.texr (256x256).

.DESCRIPTION
    Format TEXR (little-endian) : magic 0x52584554 ("TEXR"), width, height, sRGB(=1),
    puis width*height*4 octets RGBA8. Base ~RGB(58,58,66) avec bruit deterministe
    (+/-18) et lignes de joint plus sombres en grille (tous les 32 px, decalage
    alterne facon appareillage de briques) pour lire comme de la pierre taillee.
    Alpha=255.

    Remplissage du byte[] via un pointeur d'ecriture incremental ($p) pour
    fiabilite et vitesse sous Windows PowerShell 5.1.
#>

$outDir = Join-Path $PSScriptRoot "..\..\game\data\meshes\props\textures"
$outDir = [System.IO.Path]::GetFullPath($outDir)
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
$outPath = Join-Path $outDir "stone_dark.texr"

[int]$W = 256
[int]$H = 256
[int]$header = 16
$bytes = New-Object 'System.Byte[]' ($header + $W * $H * 4)

# Header LE.
[System.BitConverter]::GetBytes([uint32]0x52584554).CopyTo($bytes, 0)  # magic "TEXR"
[System.BitConverter]::GetBytes([uint32]$W).CopyTo($bytes, 4)
[System.BitConverter]::GetBytes([uint32]$H).CopyTo($bytes, 8)
[System.BitConverter]::GetBytes([uint32]1).CopyTo($bytes, 12)          # sRGB

[int]$baseR = 58; [int]$baseG = 58; [int]$baseB = 66
[int]$jointDelta = 26   # assombrissement des joints
[int]$course = 32       # hauteur de rangee / pas de grille
[int]$halfCourse = 16

# Pointeur d'ecriture (commence apres le header).
[int]$p = $header

for ([int]$y = 0; $y -lt $H; $y++) {
    # Decalage horizontal alterne d'une rangee sur deux (appareillage de briques).
    [int]$row = [math]::Floor($y / $course)
    [int]$offset = 0
    if (($row % 2) -ne 0) { $offset = $halfCourse }
    [bool]$isHJoint = (($y % $course) -lt 2)   # ligne de joint horizontale (1-2 px)

    for ([int]$x = 0; $x -lt $W; $x++) {
        # Bruit deterministe (+/-18) via hash entier sur (x,y), borne en Int32.
        [int]$noise = (((($x * 73856093) -bxor ($y * 19349663)) -band 0x7fffffff) % 37) - 18

        [int]$r = $baseR + $noise
        [int]$g = $baseG + $noise
        [int]$b = $baseB + $noise

        # Joints (lignes plus sombres) : horizontaux + verticaux decales.
        if ($isHJoint -or (((($x + $offset) % $course)) -lt 2)) {
            $r -= $jointDelta; $g -= $jointDelta; $b -= $jointDelta
        }

        if ($r -lt 0) { $r = 0 } elseif ($r -gt 255) { $r = 255 }
        if ($g -lt 0) { $g = 0 } elseif ($g -gt 255) { $g = 255 }
        if ($b -lt 0) { $b = 0 } elseif ($b -gt 255) { $b = 255 }

        $bytes[$p]     = [byte]$r
        $bytes[$p + 1] = [byte]$g
        $bytes[$p + 2] = [byte]$b
        $bytes[$p + 3] = [byte]255
        $p += 4
    }
}

[System.IO.File]::WriteAllBytes($outPath, $bytes)
Write-Output "stone_dark.texr ecrit : $outPath ($($bytes.Length) octets, p_final=$p)"
