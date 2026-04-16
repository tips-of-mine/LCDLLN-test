# Génère les binaires sous game/data/zones/demo_plains/ (tickets 007 + 012 : heightmap, splat SLAP, masque GRMS, zone, chunks…).
# Usage : depuis la racine du dépôt :
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\world\Generate-DemoPlainsAssets.ps1

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Zone = Join-Path $Root "game\data\zones\demo_plains"
New-Item -ItemType Directory -Force -Path (Join-Path $Zone "chunks\chunk_0_0") | Out-Null

function Write-VersionHeader([System.IO.BinaryWriter]$bw, [uint32]$magic, [uint32]$fmtVer, [uint64]$contentHash) {
  $bw.Write([uint32]$magic)
  $bw.Write([uint32]$fmtVer)
  $bw.Write([uint32]1) # kZoneBuilderVersion
  $bw.Write([uint32]1) # kZoneEngineVersion
  $bw.Write([uint64]$contentHash)
}

# --- terrain_height.r16h : 64×64 HAMP ---
$w = 64; $h = 64
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([uint32]0x504D4148)
$bw.Write([uint32]$w)
$bw.Write([uint32]$h)
for ($i = 0; $i -lt ($w * $h); $i++) { $bw.Write([uint16]32768) }
$bw.Flush()
[IO.File]::WriteAllBytes((Join-Path $Zone "terrain_height.r16h"), $ms.ToArray())
$bw.Close(); $ms.Close()

# --- zone.meta ---
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
Write-VersionHeader $bw ([uint32]0x454E4F5A) ([uint32]1) ([uint64]0)
$bw.Write([uint32]1) # chunkCount
$bw.Write([int32]0)
$bw.Write([int32]0)
$bw.Flush()
[IO.File]::WriteAllBytes((Join-Path $Zone "zone.meta"), $ms.ToArray())
$bw.Close(); $ms.Close()

# --- probes.bin ---
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
Write-VersionHeader $bw ([uint32]0x424F5250) ([uint32]1) ([uint64]0)
$bw.Write([uint32]1)
$bw.Write([float]5000.0); $bw.Write([float]2.0); $bw.Write([float]5000.0)
$bw.Write([float]5000.0)
$bw.Write([float]5000.0); $bw.Write([float]256.0); $bw.Write([float]5000.0)
$bw.Write([float]1.0)
$bw.Write([float]1.0); $bw.Write([float]0.0); $bw.Write([float]0.0); $bw.Write([float]0.0)
$bw.Flush()
[IO.File]::WriteAllBytes((Join-Path $Zone "probes.bin"), $ms.ToArray())
$bw.Close(); $ms.Close()

# --- atmosphere.json ---
$atm = @'
{
  "version": 1,
  "sun": {
    "direction": [0.5774, 0.5774, 0.5774],
    "color": [1.0, 0.95, 0.85]
  },
  "ambient": {
    "color": [0.03, 0.03, 0.05]
  }
}
'@
[IO.File]::WriteAllText((Join-Path $Zone "atmosphere.json"), $atm.TrimStart(), [Text.UTF8Encoding]::new($false))

# --- chunk.meta ---
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
Write-VersionHeader $bw ([uint32]0x4B4E4843) ([uint32]1) ([uint64]0)
$bw.Write([int32]0); $bw.Write([int32]0)
$bw.Write([uint32]0); $bw.Write([uint32]0); $bw.Write([uint32]500); $bw.Write([uint32]500)
$bw.Write([uint32]4) # kChunkMetaHasInstances
$bw.Flush()
[IO.File]::WriteAllBytes((Join-Path $Zone "chunks\chunk_0_0\chunk.meta"), $ms.ToArray())
$bw.Close(); $ms.Close()

# --- instances.bin (0 instance) ---
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
Write-VersionHeader $bw ([uint32]0x54534E49) ([uint32]1) ([uint64]0)
$bw.Write([uint32]0)
$bw.Flush()
[IO.File]::WriteAllBytes((Join-Path $Zone "chunks\chunk_0_0\instances.bin"), $ms.ToArray())
$bw.Close(); $ms.Close()

# --- terrain_splat.slap (256×256, bande « route » terre — ticket 012 / flux WE) ---
$sw = 256; $sh = 256
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([uint32]0x50414C53)
$bw.Write([uint32]$sw)
$bw.Write([uint32]$sh)
for ($iz = 0; $iz -lt $sh; $iz++) {
  for ($ix = 0; $ix -lt $sw; $ix++) {
    $r = [byte]255; $g = [byte]0; $b = [byte]0; $a = [byte]0
    if ([math]::Abs($ix - 128) -le 20) { $r = [byte]60; $g = [byte]195; $b = [byte]0; $a = [byte]0 }
    $bw.Write($r); $bw.Write($g); $bw.Write($b); $bw.Write($a)
  }
}
$bw.Flush()
[IO.File]::WriteAllBytes((Join-Path $Zone "terrain_splat.slap"), $ms.ToArray())
$bw.Close(); $ms.Close()

# --- terrain_grass.grms (256×256 R8, bande légère — ticket 010 / 012) ---
$gw = 256; $gh = 256
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([uint32]0x47524D53)
$bw.Write([uint32]$gw)
$bw.Write([uint32]$gh)
$buf = New-Object byte[] ($gw * $gh)
for ($iz = 0; $iz -lt $gh; $iz++) {
  for ($ix = 0; $ix -lt $gw; $ix++) {
    $idx = $iz * $gw + $ix
    if ([math]::Abs($ix - 128) -le 18) { $buf[$idx] = [byte]200 }
  }
}
$bw.Write($buf)
$bw.Flush()
[IO.File]::WriteAllBytes((Join-Path $Zone "terrain_grass.grms"), $ms.ToArray())
$bw.Close(); $ms.Close()

Write-Host "OK -> $Zone"
