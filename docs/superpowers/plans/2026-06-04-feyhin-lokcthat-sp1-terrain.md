# Feyhin Lokcthat — SP1 (Terrain & hydrographie) — Plan d'implémentation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Générer et brancher une nouvelle zone `feyhin_lokcthat` (relief + eau + surfaces) comme carte de lancement, praticable à pied, fidèle au plan de masse v7.

**Architecture:** Un unique script PowerShell paramétrique (`Generate-FeyhinLokcthatAssets.ps1`) écrit tous les binaires de la zone (HAMP/SLAP/GRMS + zone.meta + runtime_manifest.json + atmosphere.json) et un aperçu HTML d'ombrage. L'eau est une nappe plate posée via la config `world.test_water` (pas de `water.bin`, donc pas de xxHash64 à réimplémenter). Le terrain est lu directement par le client depuis `render.terrain.*` (manifeste legacy single-zone) — **aucun `zone_builder`**. La marche est automatique (collision heightmap). Le repointage se fait dans `config.json`.

**Tech Stack:** PowerShell 5.1 (génération binaire little-endian via `[byte[]]`), JSON, HTML/SVG/Canvas (aperçu). Aucun build C++.

**Spec :** `docs/superpowers/specs/2026-06-04-feyhin-lokcthat-sp1-terrain-design.md`. **Géo figée :** `docs/feyhin_lokcthat/plan_masse.html` v7.

**Repère monde :** X = est(+)/ouest(−), Z = nord(+)/sud(−), Y = altitude (m). Terrain carré `[-768, +768]`, heightmap 1025², `height_scale = 512`, niveau d'eau **60 m**.

---

## Structure des fichiers

| Fichier | Rôle |
|---|---|
| `tools/world/Generate-FeyhinLokcthatAssets.ps1` (créé) | Générateur unique : relief, splat, herbe, méta, manifeste, atmosphère, aperçu. |
| `game/data/zones/feyhin_lokcthat/terrain_height.r16h` (généré) | HAMP 1025² u16. |
| `game/data/zones/feyhin_lokcthat/terrain_splat.slap` (généré) | SLAP RGBA8 (R=dirt, G=grass, B=rock, A=snow). |
| `game/data/zones/feyhin_lokcthat/terrain_grass.grms` (généré) | GRMS R8 (densité herbe). |
| `game/data/zones/feyhin_lokcthat/atmosphere.json` (généré) | Soleil + ambiant. |
| `game/data/zones/feyhin_lokcthat/zone.meta` (généré) | En-tête zone (ZONE). |
| `game/data/zones/feyhin_lokcthat/runtime_manifest.json` (généré) | Manifeste legacy v3. |
| `docs/feyhin_lokcthat/apercu_relief.html` (généré) | Ombrage du relief, validation hors-jeu. |
| `config.json` (modifié) | Repointage terrain + bloc `terrain` + `world.test_water` + spawn + neutralisation `world.scenery`. |

**Convention binaire** : tous les entiers little-endian (BinaryWriter Windows l'est par défaut ; ici on remplit un `[byte[]]` à la main pour la perf). Encodage hauteur : `u16 = round(clamp(h_m,0,512)/512*65535)`.

**Mapping canal SLAP** (à CONFIRMER en Task 3, step 1) : le format legacy SLAP est RGBA = 4 poids de couche. Hypothèse retenue d'après `config.json` l.74 (« splat 4 couches grass/dirt/rock/snow ») et `Generate-DemoPlainsAssets.ps1` (R=255 par défaut = terre/dirt, bande verte G = grass) : **R=dirt(0), G=grass(1), B=rock(5), A=snow(6)**. Les constantes `$CH_*` du script isolent ce mapping ; si le shader terrain révèle un autre ordre, seules ces 4 lignes changent.

---

## Task 1 : Squelette du générateur + champ de hauteurs (HAMP)

**Files:**
- Create: `tools/world/Generate-FeyhinLokcthatAssets.ps1`
- Génère: `game/data/zones/feyhin_lokcthat/terrain_height.r16h`

- [ ] **Step 1 : Écrire le script (paramètres + helpers + synthèse relief + écriture HAMP)**

```powershell
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

# Hauteur (m) d'un point monde : MAX des reliefs, puis MIN (carve rivière).
function HeightAt([double]$wx, [double]$wz) {
  # Plancher (plan d'eau au sud pour inonder le premier plan)
  $h = $FloorBase
  if ($wz -lt 40.0) { $h = $PondFloor + ($FloorBase - $PondFloor) * (Smooth(($wz + 60.0) / 100.0)) }

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
  # Pic de la cité (DÉTACHÉ) : crag à plateau ~410, centré (132, 300), flancs raides
  $dx = ($wx - 132.0) / 50.0; $dz = ($wz - 300.0) / 110.0
  $r  = [math]::Sqrt($dx*$dx + $dz*$dz)
  if ($r -lt 1.0) { $cand = 410.0 } else { $cand = 410.0 - ($r - 1.0) * 360.0 }
  if ($cand -gt $h) { $h = $cand }
  # Éperon monument : bosse ~180, centré (55, 345)
  $mx = ($wx - 55.0) / 22.0; $mz = ($wz - 345.0) / 22.0
  $mr = [math]::Sqrt($mx*$mx + $mz*$mz)
  if ($mr -lt 1.0) { $cand = 180.0 } else { $cand = 180.0 - ($mr - 1.0) * 300.0 }
  if ($cand -gt $h) { $h = $cand }
  # Crête de fond (nord)
  if ($wz -ge 600.0) {
    $t = Smooth(($wz - 600.0) / 120.0)
    $cand = $FloorBase + (250.0 - $FloorBase) * $t
    if ($cand -gt $h) { $h = $cand }
  }

  # Carve rivière (MIN) : lit à PondFloor au centre, remonte au niveau d'eau aux berges
  $d = DistToMeander $wx $wz
  $hw = HalfWidthAtZ $wz
  if ($d -lt $hw) {
    $channel = $PondFloor + ($WaterLevel - $PondFloor) * ($d / $hw)
    if ($channel -lt $h) { $h = $channel }
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
```

- [ ] **Step 2 : Exécuter le générateur**

Run : `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\world\Generate-FeyhinLokcthatAssets.ps1`
Expected : affiche « HAMP écrit (1025 x 1025). » sans erreur. (Durée : la synthèse 1025² en PS peut prendre quelques minutes — normal. Pour itérer plus vite, baisser temporairement `$Res=513`.)

- [ ] **Step 3 : Valider l'en-tête HAMP (readback)**

Run :
```powershell
$b=[IO.File]::ReadAllBytes("game/data/zones/feyhin_lokcthat/terrain_height.r16h")
"magic=0x{0:X8} w={1} h={2} size={3} attendu={4}" -f `
 [BitConverter]::ToUInt32($b,0), [BitConverter]::ToUInt32($b,4), [BitConverter]::ToUInt32($b,8), `
 $b.Length, (12 + 1025*1025*2)
```
Expected : `magic=0x504D4148 w=1025 h=1025 size=2101262 attendu=2101262`

- [ ] **Step 4 : Commit**

```bash
git add tools/world/Generate-FeyhinLokcthatAssets.ps1 game/data/zones/feyhin_lokcthat/terrain_height.r16h
git commit -m "feat(world): SP1 Feyhin — generateur PS + heightmap HAMP 1025"
```

---

## Task 2 : Aperçu d'ombrage HTML (validation hors-jeu)

**Files:**
- Modify: `tools/world/Generate-FeyhinLokcthatAssets.ps1` (ajout en fin de script)
- Génère: `docs/feyhin_lokcthat/apercu_relief.html`

- [ ] **Step 1 : Ajouter l'émission de l'aperçu (grille sous-échantillonnée + hillshade)**

Ajouter à la fin du script (après l'écriture HAMP) :

```powershell
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
```

- [ ] **Step 2 : Régénérer et ouvrir l'aperçu**

Run : `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\world\Generate-FeyhinLokcthatAssets.ps1`
Expected : « Aperçu écrit ». Ouvrir `docs/feyhin_lokcthat/apercu_relief.html` dans le navigateur.

- [ ] **Step 3 : Vérifier la forme (vs plan v7)**

Contrôle visuel : vallée N-S, méandre bleu central qui se resserre, versant ouest (gauche) montant, **pic de la cité détaché** (tache claire isolée à droite-centre) **séparé** du massif est (mur à droite) par une selle plus basse, éperon monument (petite tache) près de la rivière, plan d'eau au sud. Si la forme dévie, ajuster les paramètres de `HeightAt` et relancer.

- [ ] **Step 4 : Commit**

```bash
git add tools/world/Generate-FeyhinLokcthatAssets.ps1 docs/feyhin_lokcthat/apercu_relief.html
git commit -m "feat(world): SP1 Feyhin — apercu HTML hillshade du relief"
```

---

## Task 3 : Splat (SLAP) par règles altitude/pente

**Files:**
- Modify: `tools/world/Generate-FeyhinLokcthatAssets.ps1`
- Génère: `game/data/zones/feyhin_lokcthat/terrain_splat.slap`

- [ ] **Step 1 : Confirmer le mapping canal→couche**

Lire `game/data/shaders/terrain_chunk.vert`/`.frag` et/ou `src/client/render/terrain/SplatSampling.cpp` pour vérifier quel canal RGBA correspond à quelle couche du `layer_palette.json`. Si l'ordre diffère de `R=dirt, G=grass, B=rock, A=snow`, ajuster les 4 constantes `$CH_*` ci-dessous (et rien d'autre).

- [ ] **Step 2 : Ajouter la synthèse SLAP**

Ajouter après l'écriture HAMP (avant ou après l'aperçu, peu importe) :

```powershell
# ----------------- SPLAT (SLAP RGBA8) -----------------
# Canaux (mapping legacy, cf. Task 3 step 1) : R=dirt, G=grass, B=rock, A=snow
$CH_DIRT=0; $CH_GRASS=1; $CH_ROCK=2; $CH_SNOW=3
$sw = $SplatRes; $sh = $SplatRes
$sb = New-Object 'byte[]' (12 + $sw*$sh*4)
[BitConverter]::GetBytes([uint32]0x50414C53).CopyTo($sb,0)  # 'SLAP'
[BitConverter]::GetBytes([uint32]$sw).CopyTo($sb,4)
[BitConverter]::GetBytes([uint32]$sh).CopyTo($sb,8)
$so = 12
for ($iz = 0; $iz -lt $sh; $iz++) {
  $wz = $Origin + ($iz / ($sh - 1.0)) * $WorldSize
  for ($ix = 0; $ix -lt $sw; $ix++) {
    $wx = $Origin + ($ix / ($sw - 1.0)) * $WorldSize
    $h  = HeightAt $wx $wz
    # pente approx via différences finies (pas monde du splat)
    $stepM = $WorldSize / ($sw - 1.0)
    $hx = (HeightAt ($wx + $stepM) $wz) - (HeightAt ($wx - $stepM) $wz)
    $hz = (HeightAt $wx ($wz + $stepM)) - (HeightAt $wx ($wz - $stepM))
    $slope = [math]::Sqrt($hx*$hx + $hz*$hz) / (2.0 * $stepM)  # ~ tan(pente)
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
```

- [ ] **Step 3 : Régénérer et valider l'en-tête SLAP**

Run le générateur, puis :
```powershell
$b=[IO.File]::ReadAllBytes("game/data/zones/feyhin_lokcthat/terrain_splat.slap")
"magic=0x{0:X8} w={1} h={2} size={3} attendu={4}" -f `
 [BitConverter]::ToUInt32($b,0),[BitConverter]::ToUInt32($b,4),[BitConverter]::ToUInt32($b,8), `
 $b.Length,(12+513*513*4)
```
Expected : `magic=0x50414C53 w=513 h=513 size=1052688 attendu=1052688`

- [ ] **Step 4 : Commit**

```bash
git add tools/world/Generate-FeyhinLokcthatAssets.ps1 game/data/zones/feyhin_lokcthat/terrain_splat.slap
git commit -m "feat(world): SP1 Feyhin — splat SLAP par regles altitude/pente"
```

---

## Task 4 : Masque herbe (GRMS)

**Files:**
- Modify: `tools/world/Generate-FeyhinLokcthatAssets.ps1`
- Génère: `game/data/zones/feyhin_lokcthat/terrain_grass.grms`

- [ ] **Step 1 : Ajouter la synthèse GRMS**

```powershell
# ----------------- HERBE DÉTAIL (GRMS R8) -----------------
$gw = $SplatRes; $gh = $SplatRes
$gb = New-Object 'byte[]' (12 + $gw*$gh)
[BitConverter]::GetBytes([uint32]0x47524D53).CopyTo($gb,0)  # 'GRMS'
[BitConverter]::GetBytes([uint32]$gw).CopyTo($gb,4)
[BitConverter]::GetBytes([uint32]$gh).CopyTo($gb,8)
$go = 12
for ($iz = 0; $iz -lt $gh; $iz++) {
  $wz = $Origin + ($iz / ($gh - 1.0)) * $WorldSize
  for ($ix = 0; $ix -lt $gw; $ix++) {
    $wx = $Origin + ($ix / ($gw - 1.0)) * $WorldSize
    $h  = HeightAt $wx $wz
    $stepM = $WorldSize / ($gw - 1.0)
    $hx = (HeightAt ($wx + $stepM) $wz) - (HeightAt ($wx - $stepM) $wz)
    $hz = (HeightAt $wx ($wz + $stepM)) - (HeightAt $wx ($wz - $stepM))
    $slope = [math]::Sqrt($hx*$hx + $hz*$hz) / (2.0 * $stepM)
    $v = 0
    if ($h -gt ($WaterLevel + 3.0) -and $h -lt 250.0 -and $slope -lt 0.6) { $v = 200 }
    $gb[$go] = [byte]$v; $go++
  }
}
[IO.File]::WriteAllBytes((Join-Path $Zone "terrain_grass.grms"), $gb)
Write-Host "[Feyhin] GRMS écrit ($gw x $gh)."
```

- [ ] **Step 2 : Régénérer et valider**

Run le générateur, puis :
```powershell
$b=[IO.File]::ReadAllBytes("game/data/zones/feyhin_lokcthat/terrain_grass.grms")
"magic=0x{0:X8} w={1} h={2} size={3} attendu={4}" -f `
 [BitConverter]::ToUInt32($b,0),[BitConverter]::ToUInt32($b,4),[BitConverter]::ToUInt32($b,8), `
 $b.Length,(12+513*513)
```
Expected : `magic=0x47524D53 w=513 h=513 size=263181 attendu=263181`

- [ ] **Step 3 : Commit**

```bash
git add tools/world/Generate-FeyhinLokcthatAssets.ps1 game/data/zones/feyhin_lokcthat/terrain_grass.grms
git commit -m "feat(world): SP1 Feyhin — masque herbe GRMS"
```

---

## Task 5 : atmosphère + zone.meta + runtime_manifest.json

**Files:**
- Modify: `tools/world/Generate-FeyhinLokcthatAssets.ps1`
- Génère: `atmosphere.json`, `zone.meta`, `runtime_manifest.json`

- [ ] **Step 1 : Ajouter l'écriture des trois fichiers**

```powershell
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
```

- [ ] **Step 2 : Régénérer et valider**

Run le générateur, puis :
```powershell
$z=[IO.File]::ReadAllBytes("game/data/zones/feyhin_lokcthat/zone.meta")
"zonemeta magic=0x{0:X8} chunks={1}" -f [BitConverter]::ToUInt32($z,0),[BitConverter]::ToUInt32($z,24)
Get-Content "game/data/zones/feyhin_lokcthat/runtime_manifest.json" -Raw | ConvertFrom-Json | Select zone_id, terrain_world_size_m
Get-Content "game/data/zones/feyhin_lokcthat/atmosphere.json" -Raw | ConvertFrom-Json | Select version
```
Expected : `zonemeta magic=0x454E4F5A chunks=1`, `zone_id=feyhin_lokcthat terrain_world_size_m=1536`, `version=1`.

- [ ] **Step 3 : Commit**

```bash
git add tools/world/Generate-FeyhinLokcthatAssets.ps1 game/data/zones/feyhin_lokcthat/atmosphere.json game/data/zones/feyhin_lokcthat/zone.meta game/data/zones/feyhin_lokcthat/runtime_manifest.json
git commit -m "feat(world): SP1 Feyhin — atmosphere + zone.meta + manifest"
```

---

## Task 6 : Bascule `config.json` (terrain, échelle, eau, spawn, scenery)

**Files:**
- Modify: `config.json`

> Sauvegarde d'abord : `Copy-Item config.json config.json.bak`. Toutes les éditions ci-dessous sont des modifications JSON ; après chaque groupe, vérifier `Get-Content config.json -Raw | ConvertFrom-Json | Out-Null` (aucune erreur = JSON valide).

- [ ] **Step 1 : Repointer `render.terrain.*` vers la zone Feyhin**

Dans le bloc `"render"` → `"terrain"` (≈ l.73-80), remplacer les 3 chemins :
```json
            "heightmap": "zones/feyhin_lokcthat/terrain_height.r16h",
            "splatmap": "zones/feyhin_lokcthat/terrain_splat.slap",
            "grass_mask": "zones/feyhin_lokcthat/terrain_grass.grms",
```
(laisser `grass_mask_visual_strength` et `hole_mask` inchangés.)

- [ ] **Step 2 : Ajouter un bloc top-level `terrain` (échelle/encodage)**

Le renderer lit `terrain.world_size/height_scale/origin_x/origin_z` SANS préfixe `render` (actuellement absents → défauts 1024/200/-512). Ajouter un bloc top-level `"terrain"` (p.ex. juste après le bloc `"render": { ... }` fermant, l.~81) :
```json
    "terrain": {
        "world_size": 1536.0,
        "height_scale": 512.0,
        "origin_x": -768.0,
        "origin_z": -768.0
    },
```

- [ ] **Step 3 : Configurer l'eau (nappe plate niveau 60) via `world.test_water`**

Dans `"world"` → `"test_water"` (≈ l.522-528), remplacer par une grande nappe couvrant la vallée. Le niveau = `GroundHeightAt(center) + depth_m` ; on vise 60 m en posant le centre sur le plan d'eau du premier plan (sol ~50) et `depth_m=10` :
```json
        "test_water": {
            "enabled": true,
            "center_x": -20.0,
            "center_z": -20.0,
            "half_size_m": 820.0,
            "depth_m": 10.0
        },
```
*(half_size 820 m couvre tout le carré ±768 ; le depth-test du water.frag clippe la nappe au rivage réel. Si le niveau lu en jeu n'est pas ~60, ajuster `depth_m` — Task 7.)*

- [ ] **Step 4 : Spawn au point de vue sud**

Dans `"world"` → `"default_spawn"` (≈ l.529-536) :
```json
        "default_spawn": {
            "_comment": "Feyhin SP1 : POV cavaliers au sud, regard vers le nord (+Z).",
            "x": -30.0,
            "y": 70.0,
            "z": -40.0,
            "yaw_deg": 0.0,
            "pitch_deg": -8.0
        },
```
*(yaw_deg orientant vers +Z (nord) à confirmer/ajuster en jeu — Task 7.)*

- [ ] **Step 5 : Neutraliser les props demo (`world.scenery`)**

Les props `world.scenery` (≈ l. numérotées 1..309) sont positionnés pour `demo_plains` et flotteraient/s'enterreraient sur Feyhin. Vider l'objet pour SP1 (ils seront remplacés par les vrais objets en SP3) :
```json
        "scenery": {
        },
```
*(Repérer la clé `"scenery"` parente du dictionnaire numéroté dans le bloc `"world"` et remplacer son contenu par un objet vide. Conserver la sauvegarde `config.json.bak`.)*

- [ ] **Step 6 : Valider le JSON**

Run : `powershell -NoProfile -Command "Get-Content config.json -Raw | ConvertFrom-Json | Out-Null; 'config.json OK'"`
Expected : `config.json OK` (aucune exception).

- [ ] **Step 7 : Commit**

```bash
git add config.json
git commit -m "feat(world): SP1 Feyhin — bascule carte de lancement (terrain/echelle/eau/spawn)"
```

---

## Task 7 : Validation en jeu (manuelle)

**Files:** aucun (test runtime ; nécessite un client buildé — côté utilisateur).

- [ ] **Step 1 : Lancer le client** et cliquer « Jouer ».

- [ ] **Step 2 : Checklist visuelle/jouabilité**

Vérifier :
- Le terrain est **visible** (pas le bug terrain-invisible ; si ciel uni → cf. CLAUDE.md « winding » — mais SP1 ne touche aucun pipeline, donc improbable).
- Spawn au **sud**, regard vers le nord ; la vallée s'ouvre devant (méandre, pic de la cité détaché à droite, massif derrière).
- **Eau** présente au niveau ~60 m (river + plan d'eau du premier plan) ; on peut s'y avancer (gué/nage).
- Marche : descendre vers l'eau, longer la rivière, gravir le versant ouest, atteindre l'emplacement du pont (pas de pont — SP2), le massif barre l'est.
- Pas de props demo flottants.

- [ ] **Step 3 : Ajustements si besoin**

Si l'orientation du regard, le niveau d'eau ou une cote du relief ne va pas : ajuster les paramètres (`HeightAt`, `world.default_spawn.yaw_deg`, `world.test_water.depth_m`), relancer le générateur si le relief change, et re-tester. Re-commit les fichiers modifiés.

- [ ] **Step 4 : Marquer SP1 terminé**

Quand la checklist passe, SP1 est complet. Déploiement : **client uniquement** (aucun redéploiement serveur).

---

## Self-review (couverture du spec)

- §3 coordonnées → Task 1 (`HeightAt` encode versants/pic/éperon/massif/méandre/fond) ✔
- §4 livrables → Tasks 1 (HAMP), 3 (SLAP), 4 (GRMS), 5 (atmosphere/zone.meta/manifest) ✔ ; eau → Task 6 step 3 (nappe `test_water`, **substitut documenté** de `water.bin` pour éviter xxHash64) ✔
- §5 générateur (relief/splat/herbe/atmosphère/aperçu) → Tasks 1–5 ✔
- §7 bascule + spawn → Task 6 ✔
- §8 aperçu HTML → Task 2 ✔
- §9 marche/collision → Task 7 ✔
- §10 robustesse (idempotent, magies, clamp, splat non nul) → écriture par regen + readbacks Tasks 1/3/4/5 ✔
- §11 tests → Tasks 2 (aperçu) + 7 (en jeu) ✔
- §13 déploiement client-only ✔

**Écart spec→plan assumé** : l'eau passe par `world.test_water` (nappe plate niveau 60) plutôt que `instances/water.bin`, pour ne pas réimplémenter xxHash64 en PowerShell. Même résultat (eau plate praticable). La rivière maillée WATR (largeur/flux) est reportée à un raffinement ultérieur. À refléter dans le spec §C/§4.
