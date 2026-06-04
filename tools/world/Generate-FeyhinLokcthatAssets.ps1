# Génère les assets de la zone feyhin_lokcthat (SP1 : terrain + eau + surfaces).
# Usage depuis la racine du dépôt :
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\world\Generate-FeyhinLokcthatAssets.ps1
# Géo figée par docs/feyhin_lokcthat/plan_masse.html (v7). Repère : X est(+)/ouest(-), Z nord(+)/sud(-).
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Zone = Join-Path $Root "game\data\zones\feyhin_lokcthat"
New-Item -ItemType Directory -Force -Path $Zone | Out-Null

# ----------------- PARAMÈTRES -----------------
$Res         = 1025          # côté heightmap (1024 gaps -> vertStep = worldSize/1024)
$WorldSize   = 1536.0        # côté monde (m), carré
$Origin      = -768.0        # origine X et Z (centré)
$HeightScale = 512.0         # hauteur max encodée par u16=65535
$WaterLevel  = 60.0          # niveau de l'eau (m)
$FloorBase   = 64.0          # plancher de vallée sec (m), juste au-dessus de l'eau
$PondFloor   = 50.0          # fond du plan d'eau / lit (m), sous l'eau
$SplatRes    = 513           # résolution splat/herbe (suffisant, plus rapide que 1025)

# Spline du méandre : waypoints (Z, X) du nord (640) au sud (-60), demi-largeur par waypoint.
$RZ = @(640.0, 560.0, 470.0, 380.0, 300.0, 210.0, 120.0, 40.0, -60.0)
$RX = @( 30.0,  85.0,  55.0,   5.0, -45.0,   5.0,  55.0, 20.0, -15.0)
$RHW= @( 60.0,  62.0,  64.0,  66.0,  72.0,  70.0,  66.0, 72.0,  90.0)  # demi-largeur (m)

# ----------------- HELPERS -----------------
function TexelToWorld([int]$i) { return $Origin + ($i / ($Res - 1)) * $WorldSize }
function Smooth([double]$t) { if ($t -lt 0) { $t = 0 } elseif ($t -gt 1) { $t = 1 }; return $t * $t * (3.0 - 2.0 * $t) }

# Demi-largeur de rivière interpolée par Z (RZ décroissant).
function HalfWidthAtZ([double]$z) {
  if ($z -ge $RZ[0]) { return $RHW[0] }
  if ($z -le $RZ[$RZ.Count-1]) { return $RHW[$RHW.Count-1] }
  for ($k = 0; $k -lt $RZ.Count-1; $k++) {
    if ($z -le $RZ[$k] -and $z -ge $RZ[$k+1]) {
      $t = ($RZ[$k] - $z) / ($RZ[$k] - $RZ[$k+1])
      return $RHW[$k] + ($RHW[$k+1] - $RHW[$k]) * $t
    }
  }
  return $RHW[0]
}

# Distance 2D (XZ) d'un point à la polyligne du méandre.
function DistToMeander([double]$wx, [double]$wz) {
  $best = [double]::MaxValue
  for ($k = 0; $k -lt $RX.Count-1; $k++) {
    $ax = $RX[$k];   $az = $RZ[$k]
    $bx = $RX[$k+1]; $bz = $RZ[$k+1]
    $dx = $bx - $ax; $dz = $bz - $az
    $len2 = $dx*$dx + $dz*$dz
    if ($len2 -le 1e-6) { $t = 0.0 } else {
      $t = (($wx-$ax)*$dx + ($wz-$az)*$dz) / $len2
      if ($t -lt 0) { $t = 0 } elseif ($t -gt 1) { $t = 1 }
    }
    $px = $ax + $dx*$t; $pz = $az + $dz*$t
    $ex = $wx-$px; $ez = $wz-$pz
    $d = [math]::Sqrt($ex*$ex + $ez*$ez)
    if ($d -lt $best) { $best = $d }
  }
  return $best
}

# Hauteur (m) d'un point monde. Modèle : (1) plancher de vallée, (2) carve de la rivière
# DANS le plancher uniquement, (3) MAX des reliefs rocheux par-dessus — ainsi un éperon
# (monument) ou un pic (cité) n'est JAMAIS noyé par la rivière (rocher dans/au bord de l'eau).
function HeightAt([double]$wx, [double]$wz) {
  # 1) Plancher (plan d'eau au sud pour inonder le premier plan)
  $floor = $FloorBase
  if ($wz -lt 40.0) { $floor = $PondFloor + ($FloorBase - $PondFloor) * (Smooth(($wz + 60.0) / 100.0)) }

  # 2) Carve rivière dans le plancher seulement (lit à PondFloor au centre, remonte aux berges)
  $d  = DistToMeander $wx $wz
  $hw = HalfWidthAtZ $wz
  if ($d -lt $hw) {
    $channel = $PondFloor + ($WaterLevel - $PondFloor) * ($d / $hw)
    if ($channel -lt $floor) { $floor = $channel }
  }
  $h = $floor

  # 3) Reliefs rocheux : MAX par-dessus le plancher (jamais noyés)
  # Versant ouest (boisé) : monte vers l'ouest
  if ($wx -le -120.0) {
    $t = Smooth(((-120.0) - $wx) / 430.0)
    $cand = $FloorBase + (270.0 - $FloorBase) * $t
    if ($cand -gt $h) { $h = $cand }
  }
  # Massif est (mur de fond, séparé) : monte vers l'est au-delà de X=235
  if ($wx -ge 235.0) {
    $t = Smooth(($wx - 235.0) / 300.0)
    $cand = $FloorBase + (480.0 - $FloorBase) * $t
    if ($cand -gt $h) { $h = $cand }
  }
  # Pic de la cité (DÉTACHÉ) : plateau ~410, centré (132,300), flancs étroits et raides
  $dx = ($wx - 132.0) / 42.0; $dz = ($wz - 300.0) / 95.0
  $r  = [math]::Sqrt($dx*$dx + $dz*$dz)
  if ($r -lt 1.0) { $cand = 410.0 } else { $cand = 410.0 - ($r - 1.0) * 900.0 }
  if ($cand -gt $h) { $h = $cand }
  # Éperon monument : rocher ~180, centré (55,345), flancs raides — survit à la rivière
  $mx = ($wx - 55.0) / 20.0; $mz = ($wz - 345.0) / 20.0
  $mr = [math]::Sqrt($mx*$mx + $mz*$mz)
  if ($mr -lt 1.0) { $cand = 180.0 } else { $cand = 180.0 - ($mr - 1.0) * 500.0 }
  if ($cand -gt $h) { $h = $cand }
  # Crête de fond (nord)
  if ($wz -ge 600.0) {
    $t = Smooth(($wz - 600.0) / 120.0)
    $cand = $FloorBase + (250.0 - $FloorBase) * $t
    if ($cand -gt $h) { $h = $cand }
  }
  return $h
}

# ----------------- SYNTHÈSE DU CHAMP (mètres) -----------------
Write-Host "[Feyhin] Synthèse relief $Res x $Res ..."
$heightsM = New-Object 'single[]' ($Res * $Res)
for ($iz = 0; $iz -lt $Res; $iz++) {
  $wz = TexelToWorld $iz
  $row = $iz * $Res
  for ($ix = 0; $ix -lt $Res; $ix++) {
    $wx = TexelToWorld $ix
    $heightsM[$row + $ix] = [single](HeightAt $wx $wz)
  }
}

# Lissage léger (1 passe box 3x3) anti-marches
$sm = New-Object 'single[]' ($Res * $Res)
for ($iz = 0; $iz -lt $Res; $iz++) {
  for ($ix = 0; $ix -lt $Res; $ix++) {
    $acc = 0.0; $n = 0
    for ($dz = -1; $dz -le 1; $dz++) {
      $z2 = $iz + $dz; if ($z2 -lt 0 -or $z2 -ge $Res) { continue }
      for ($dxk = -1; $dxk -le 1; $dxk++) {
        $x2 = $ix + $dxk; if ($x2 -lt 0 -or $x2 -ge $Res) { continue }
        $acc += $heightsM[$z2 * $Res + $x2]; $n++
      }
    }
    $sm[$iz * $Res + $ix] = [single]($acc / $n)
  }
}
$heightsM = $sm

# ----------------- ÉCRITURE HAMP -----------------
$hdr = 12
$bytes = New-Object 'byte[]' ($hdr + $Res * $Res * 2)
# magic HAMP=0x504D4148, width, height (uint32 LE)
[BitConverter]::GetBytes([uint32]0x504D4148).CopyTo($bytes, 0)
[BitConverter]::GetBytes([uint32]$Res).CopyTo($bytes, 4)
[BitConverter]::GetBytes([uint32]$Res).CopyTo($bytes, 8)
$o = $hdr
for ($i = 0; $i -lt ($Res*$Res); $i++) {
  $m = $heightsM[$i]
  if ($m -lt 0) { $m = 0 } elseif ($m -gt $HeightScale) { $m = $HeightScale }
  $v = [uint16][math]::Round($m / $HeightScale * 65535.0)
  $bytes[$o] = [byte]($v -band 0xFF); $bytes[$o+1] = [byte](($v -shr 8) -band 0xFF)
  $o += 2
}
[IO.File]::WriteAllBytes((Join-Path $Zone "terrain_height.r16h"), $bytes)
Write-Host "[Feyhin] HAMP écrit ($Res x $Res)."

# ----------------- APERÇU HTML (ombrage) -----------------
$Doc = Join-Path $Root "docs\feyhin_lokcthat"
New-Item -ItemType Directory -Force -Path $Doc | Out-Null
$PV = 192  # résolution de l'aperçu
$grid = New-Object 'double[]' ($PV * $PV)
for ($pz = 0; $pz -lt $PV; $pz++) {
  $iz = [int]([math]::Round($pz / ($PV - 1.0) * ($Res - 1)))
  for ($px = 0; $px -lt $PV; $px++) {
    $ix = [int]([math]::Round($px / ($PV - 1.0) * ($Res - 1)))
    $grid[$pz * $PV + $px] = [double]$heightsM[$iz * $Res + $ix]
  }
}
$json = ($grid | ForEach-Object { [math]::Round($_, 1) }) -join ","
$html = @"
<!DOCTYPE html><html lang="fr"><head><meta charset="utf-8">
<title>Feyhin Lokcthat — aperçu relief</title>
<style>body{margin:0;background:#0c0f14;color:#e8eef5;font-family:Arial}h1{font-size:16px;padding:12px}
canvas{display:block;margin:0 12px;border:1px solid #2a3340;image-rendering:pixelated}p{padding:0 12px;color:#93a1b0;font-size:12px}</style>
</head><body>
<h1>Feyhin Lokcthat — ombrage du relief (nord en haut, est à droite · niveau d'eau bleu)</h1>
<canvas id="c" width="$PV" height="$PV" style="width:768px;height:768px"></canvas>
<p>Aperçu $PV×$PV de la heightmap 1025². Bleu = sous le niveau d'eau ($WaterLevel m). Échelle hauteur 0–$HeightScale m.</p>
<script>
const PV=$PV, H=[$json], WATER=$WaterLevel, HS=$HeightScale;
const ctx=document.getElementById('c').getContext('2d'), img=ctx.createImageData(PV,PV);
function at(x,z){x=Math.max(0,Math.min(PV-1,x));z=Math.max(0,Math.min(PV-1,z));return H[z*PV+x];}
for(let z=0;z<PV;z++)for(let x=0;x<PV;x++){
  // z image (haut=0) -> nord (Z max) : on inverse pour mettre le nord en haut
  const zz=PV-1-z;
  const h=at(x,zz), hx=at(x+1,zz)-at(x-1,zz), hz=at(x,zz+1)-at(x,zz-1);
  const slope=1/Math.sqrt(1+(hx*hx+hz*hz)/16); // ombrage simple
  let r,g,b;
  if(h<WATER){ r=30;g=90;b=150; } // eau
  else { const t=Math.min(1,(h-WATER)/(HS-WATER));
    r=90+120*t; g=120+80*t-40*t; b=70+60*t; // herbe -> roche
    r*=slope; g*=slope; b*=slope; }
  const i=(z*PV+x)*4; img.data[i]=r; img.data[i+1]=g; img.data[i+2]=b; img.data[i+3]=255;
}
ctx.putImageData(img,0,0);
</script></body></html>
"@
[IO.File]::WriteAllText((Join-Path $Doc "apercu_relief.html"), $html, [Text.UTF8Encoding]::new($false))
Write-Host "[Feyhin] Aperçu écrit -> docs/feyhin_lokcthat/apercu_relief.html"

# ----------------- SPLAT (SLAP RGBA8) -----------------
# Canaux (mapping legacy, cf. Task 3 step 1) : R=grass, G=dirt, B=rock, A=snow
# CORRECTION vs plan : terrain.frag (legacy) déclare R=grass, G=dirt, B=rock, A=snow.
# Les constantes $CH_* ont été ajustées en conséquence ($CH_GRASS=0, $CH_DIRT=1).
$CH_DIRT=1; $CH_GRASS=0; $CH_ROCK=2; $CH_SNOW=3
$sw = $SplatRes; $sh = $SplatRes
$sb = New-Object 'byte[]' (12 + $sw*$sh*4)
[BitConverter]::GetBytes([uint32]0x50414C53).CopyTo($sb,0)  # 'SLAP'
[BitConverter]::GetBytes([uint32]$sw).CopyTo($sb,4)
[BitConverter]::GetBytes([uint32]$sh).CopyTo($sb,8)
$so = 12
# Échantillonne la grille de hauteurs déjà calculée ($heightsM, $Res x $Res) au lieu de
# rappeler HeightAt par pixel (perf : lookups au lieu de ~2.6M appels lourds). Pente = voisins.
$hStepS = $WorldSize / ($Res - 1.0)        # mètres par texel du heightmap (1025)
$scaleS = ($Res - 1.0) / ($sw - 1.0)       # index splat -> index heightmap
for ($iz = 0; $iz -lt $sh; $iz++) {
  $hzc = [int][math]::Round($iz * $scaleS); if ($hzc -lt 0) { $hzc = 0 } elseif ($hzc -gt ($Res-1)) { $hzc = $Res-1 }
  $zm = [math]::Max(0, $hzc-1); $zp = [math]::Min($Res-1, $hzc+1)
  for ($ix = 0; $ix -lt $sw; $ix++) {
    $hxc = [int][math]::Round($ix * $scaleS); if ($hxc -lt 0) { $hxc = 0 } elseif ($hxc -gt ($Res-1)) { $hxc = $Res-1 }
    $xm = [math]::Max(0, $hxc-1); $xp = [math]::Min($Res-1, $hxc+1)
    $h   = $heightsM[$hzc*$Res + $hxc]
    $dhx = $heightsM[$hzc*$Res + $xp] - $heightsM[$hzc*$Res + $xm]
    $dhz = $heightsM[$zp*$Res + $hxc] - $heightsM[$zm*$Res + $hxc]
    $sxx = $dhx / (($xp-$xm) * $hStepS); $szz = $dhz / (($zp-$zm) * $hStepS)
    $slope = [math]::Sqrt($sxx*$sxx + $szz*$szz)
    $rgba = @(0,0,0,0)
    if ($h -lt ($WaterLevel + 2.0)) { $rgba[$CH_DIRT] = 255 }       # bords/lit : terre/boue
    elseif ($slope -gt 0.9)         { $rgba[$CH_ROCK] = 255 }       # pente forte : roche
    elseif ($h -gt 430.0)           { $rgba[$CH_SNOW] = 255 }       # sommets : neige
    else                            { $rgba[$CH_GRASS] = 255 }      # défaut : herbe
    $sb[$so]=$rgba[0]; $sb[$so+1]=$rgba[1]; $sb[$so+2]=$rgba[2]; $sb[$so+3]=$rgba[3]
    $so += 4
  }
}
[IO.File]::WriteAllBytes((Join-Path $Zone "terrain_splat.slap"), $sb)
Write-Host "[Feyhin] SLAP écrit ($sw x $sh)."

# ----------------- HERBE DÉTAIL (GRMS R8) -----------------
$gw = $SplatRes; $gh = $SplatRes
$gb = New-Object 'byte[]' (12 + $gw*$gh)
[BitConverter]::GetBytes([uint32]0x47524D53).CopyTo($gb,0)  # 'GRMS'
[BitConverter]::GetBytes([uint32]$gw).CopyTo($gb,4)
[BitConverter]::GetBytes([uint32]$gh).CopyTo($gb,8)
$go = 12
# Idem SLAP : échantillonne $heightsM (perf).
$hStepG = $WorldSize / ($Res - 1.0)
$scaleG = ($Res - 1.0) / ($gw - 1.0)
for ($iz = 0; $iz -lt $gh; $iz++) {
  $hzc = [int][math]::Round($iz * $scaleG); if ($hzc -lt 0) { $hzc = 0 } elseif ($hzc -gt ($Res-1)) { $hzc = $Res-1 }
  $zm = [math]::Max(0, $hzc-1); $zp = [math]::Min($Res-1, $hzc+1)
  for ($ix = 0; $ix -lt $gw; $ix++) {
    $hxc = [int][math]::Round($ix * $scaleG); if ($hxc -lt 0) { $hxc = 0 } elseif ($hxc -gt ($Res-1)) { $hxc = $Res-1 }
    $xm = [math]::Max(0, $hxc-1); $xp = [math]::Min($Res-1, $hxc+1)
    $h   = $heightsM[$hzc*$Res + $hxc]
    $dhx = $heightsM[$hzc*$Res + $xp] - $heightsM[$hzc*$Res + $xm]
    $dhz = $heightsM[$zp*$Res + $hxc] - $heightsM[$zm*$Res + $hxc]
    $sxx = $dhx / (($xp-$xm) * $hStepG); $szz = $dhz / (($zp-$zm) * $hStepG)
    $slope = [math]::Sqrt($sxx*$sxx + $szz*$szz)
    $v = 0
    if ($h -gt ($WaterLevel + 3.0) -and $h -lt 250.0 -and $slope -lt 0.6) { $v = 200 }
    $gb[$go] = [byte]$v; $go++
  }
}
[IO.File]::WriteAllBytes((Join-Path $Zone "terrain_grass.grms"), $gb)
Write-Host "[Feyhin] GRMS écrit ($gw x $gh)."

# ----------------- ATMOSPHÈRE -----------------
$atm = @'
{
  "version": 1,
  "sun": {
    "direction": [-0.40, 0.45, 0.80],
    "color": [1.0, 0.93, 0.80]
  },
  "ambient": {
    "color": [0.06, 0.07, 0.10]
  }
}
'@
[IO.File]::WriteAllText((Join-Path $Zone "atmosphere.json"), $atm, [Text.UTF8Encoding]::new($false))

# ----------------- zone.meta (ZONE) -----------------
$zm = New-Object 'byte[]' 36
[BitConverter]::GetBytes([uint32]0x454E4F5A).CopyTo($zm,0)  # magic ZONE
[BitConverter]::GetBytes([uint32]1).CopyTo($zm,4)           # formatVersion
[BitConverter]::GetBytes([uint32]1).CopyTo($zm,8)           # builderVersion
[BitConverter]::GetBytes([uint32]1).CopyTo($zm,12)          # engineVersion
[BitConverter]::GetBytes([uint64]0).CopyTo($zm,16)          # contentHash (non validé pour zone.meta)
[BitConverter]::GetBytes([uint32]1).CopyTo($zm,24)          # chunkCount = 1
[BitConverter]::GetBytes([int32]0).CopyTo($zm,28)           # chunk i
[BitConverter]::GetBytes([int32]0).CopyTo($zm,32)           # chunk j
[IO.File]::WriteAllBytes((Join-Path $Zone "zone.meta"), $zm)

# ----------------- runtime_manifest.json (legacy v3) -----------------
$man = @"
{
  "lcdlln_runtime_manifest_version": 3,
  "zone_id": "feyhin_lokcthat",
  "terrain_heightmap": "zones/feyhin_lokcthat/terrain_height.r16h",
  "terrain_splatmap": "zones/feyhin_lokcthat/terrain_splat.slap",
  "terrain_grass_mask": "zones/feyhin_lokcthat/terrain_grass.grms",
  "source_edit_format_version": 1,
  "terrain_world_size_m": $WorldSize,
  "texture_assets": [],
  "exported_textures": [],
  "texture_assets_source_missing": [],
  "object_prefab_ids": [],
  "note": "SP1 Feyhin Lokcthat — genere par tools/world/Generate-FeyhinLokcthatAssets.ps1"
}
"@
[IO.File]::WriteAllText((Join-Path $Zone "runtime_manifest.json"), $man, [Text.UTF8Encoding]::new($false))
Write-Host "[Feyhin] atmosphere.json + zone.meta + runtime_manifest.json écrits."
