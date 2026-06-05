<#
.SYNOPSIS
    Genere la "cite haute" de Feyhin Lokcthat : une ville dense perchee sur le
    plateau de l'eperon rocheux detache, melant un amas de petites maisons
    (meme gabarit que le faubourg) et un groupe de monuments centraux (tours de
    pierre + fleche de cathedrale) qui dominent les toits. PREMIERE iteration
    (data-gen only) — geometrie correcte et placement coherent priorises ;
    l'apparence fine sera ajustee en jeu.

.DESCRIPTION
    Mecanisme : tout est pose comme des entrees `world.scenery` dans config.json
    (lues par le client, sans zone_builder). Chaque maison partage une meme
    hauteur de base `y` (sol echantillonne au centre) pour s'aligner sur le
    plateau ; chaque anneau de tour/fleche est empile via `y` explicite.

    Le script :
      1. Lit le heightmap SP1 (terrain_height.r16h, format HAMP).
      2. Choisit ~35-50 centres de maison sur le plateau du crag detache
         (X dans [+90,+180], Z dans [+210,+400]) ou le sol est >= ~330 m,
         espaces d'au moins ~6 m (densite serree).
      3. Compose chaque maison (boite 4x4 m, meme gabarit 6 pieces que le
         faubourg : dalle + 4 murs platre dont 1 porte + toit tuiles).
      4. Au centre du plateau (~X+135, Z+300) dresse 2-3 hautes tours de pierre
         (~30-40 m : anneaux Wall_UnevenBrick + toit Roof_Tower) et 1 fleche
         fine et tres haute (~50-60 m : empreinte ~3x3 m, anneaux minces + cap).
      5. Reecrit UNIQUEMENT le bloc world.scenery de config.json en conservant
         intactes les 221 entrees existantes (indices 0-220 : pont + faubourg +
         monument), nouvelles entrees a partir de l'index 221, count mis a jour.
         Sortie en LF, UTF-8 sans BOM (preserve les accents des commentaires).

    NE COMMET RIEN (le controleur gere git).

.NOTES
    PowerShell 5.1. Generation de donnees uniquement.
    Dims extraites des meshes (AABB local, m) :
      Wall_Plaster_Straight / _Door_Flat : X[-1,1]=2 m large, Y[0,3.12] haut.
      Wall_UnevenBrick_Straight          : X[-1,1]=2 m large, Y[0,3.1227] haut.
      Floor_Brick                        : X[-1,1] Z[-1,1] = 2x2 m, dalle Y~0.
      Roof_RoundTiles_4x4                : X ~5.1352 m base.
      Roof_Tower_RoundTiles              : X ~5.65052 m base, cone.
#>

[CmdletBinding()]
param(
    # Racine du worktree (contient config.json + game/).
    [string]$Root = "D:\Users\thedj\git\LCDLLN-wt-sp5",
    # Graine deterministe pour le placement des maisons.
    [int]$Seed = 1789
)

$ErrorActionPreference = 'Stop'

# --- Chemins -----------------------------------------------------------------
$ConfigPath = Join-Path $Root 'config.json'
$HeightPath = Join-Path $Root 'game\data\zones\feyhin_lokcthat\terrain_height.r16h'
$TexDir     = Join-Path $Root 'game\data\meshes\props\textures'

# --- Constantes monde / heightmap (SP1 Feyhin) -------------------------------
$WorldSize   = 1536.0            # cote du carre monde, metres
$Origin      = -768.0            # coin -X/-Z du heightmap, metres
$HmWidth     = 1025              # texels par cote
$HmHeader    = 12                # uint32 magic + uint32 width + uint32 height
$HeightScale = 512.0             # hauteur metres = u16/65535*512

# --- Zone cite haute (plateau du crag detache) -------------------------------
$XMin = 90.0;  $XMax = 180.0
$ZMin = 210.0; $ZMax = 400.0
$GroundMin   = 330.0            # sous = flanc bas du crag -> rejet (on veut le plateau)
$MaxSlope    = 25.0            # denivele max (m) sur l'empreinte
$MinSpacing  = 6.0            # distance min centre-a-centre (densite serree)
$TargetHouses = 50           # cible haute (le plateau valide peut en livrer moins)

# --- Dims maisons (meme gabarit que le faubourg) -----------------------------
$WallWidth   = 2.0            # largeur module mur (X local)
$WallHeight  = 3.12           # hauteur mur platre (Y local max)
$FloorHalf   = 1.0           # demi-cote dalle (X/Z local) -> 2x2 m a scale 1
$RoofBaseX   = 5.1352        # largeur base toit tuiles (X) a scale 1
$HouseSize   = 4.0
$WallScale   = $HouseSize / $WallWidth        # 2.0 -> mur de 4 m
$FloorScale  = $HouseSize / ($FloorHalf*2)    # 2.0 -> dalle 4x4 m
$RoofScale   = $HouseSize / $RoofBaseX        # ~0.779 -> toit couvrant ~4 m
$HalfHouse   = $HouseSize / 2.0               # 2.0 m, offset centre->mur

# --- Dims monuments (tours + fleche, pierre) ---------------------------------
$BrickHeight   = 3.1227       # hauteur geometrique mur brique -> pas d'empilement
$TowerRoofBaseX = 5.65052     # largeur base toit conique (X) a scale 1

# Grandes tours : carre ~5 m, ~11 anneaux x 3.12 -> ~34 m.
$TowerSize     = 5.0
$TowerWallScale = $TowerSize / $WallWidth     # 2.5 -> mur de 5 m
$TowerRoofScale = $TowerSize / $TowerRoofBaseX # ~0.885 -> cone couvrant ~5 m
$HalfTower     = $TowerSize / 2.0             # 2.5 m
$TowerRings    = 11                           # ~34 m

# Fleche (cathedrale) : carre ~3 m, ~18 anneaux minces + cap -> ~56 m.
$SpireSize     = 3.0
$SpireWallScale = $SpireSize / $WallWidth     # 1.5 -> mur de 3 m
$SpireRoofScale = $SpireSize / $TowerRoofBaseX # ~0.531 -> cap pointu couvrant ~3 m
$HalfSpire     = $SpireSize / 2.0             # 1.5 m
$SpireRings    = 18                           # ~56 m

# Centre du plateau pour les monuments.
$CenterX = 135.0
$CenterZ = 300.0

# --- Albedos -----------------------------------------------------------------
$AlbWall  = 'meshes/props/textures/plaster_light.texr'
$AlbRoof  = 'meshes/props/textures/roof_tile.texr'
$AlbFloor = 'meshes/props/textures/stone_dark.texr'
$AlbStone = 'meshes/props/textures/stone_dark.texr'

# =============================================================================
# 1) Lecture du heightmap
# =============================================================================
# Charge le heightmap HAMP en memoire et expose un echantillonnage par texel.
# Effet de bord : aucun (lecture seule).
$hmBytes = [System.IO.File]::ReadAllBytes($HeightPath)
$magic = [BitConverter]::ToUInt32($hmBytes, 0)
$w     = [BitConverter]::ToUInt32($hmBytes, 4)
$h     = [BitConverter]::ToUInt32($hmBytes, 8)
if ($w -ne $HmWidth -or $h -ne $HmWidth) {
    throw "Heightmap inattendu : width=$w height=$h (attendu $HmWidth)."
}
$expected = $HmHeader + $w * $h * 2
if ($hmBytes.Length -lt $expected) {
    throw "Heightmap tronque : $($hmBytes.Length) octets, attendu >= $expected."
}
Write-Host ("[Heightmap] magic=0x{0:X8} w={1} h={2} ({3} octets)" -f $magic, $w, $h, $hmBytes.Length)

# Convertit une coordonnee monde (X ou Z) en index texel (0..1024), clampe.
function Get-Texel([double]$world) {
    $t = [math]::Round( ($world - $Origin) / $WorldSize * ($HmWidth - 1) )
    if ($t -lt 0) { return 0 }
    if ($t -gt ($HmWidth - 1)) { return ($HmWidth - 1) }
    return [int]$t
}

# Hauteur sol (m) au point monde (worldX, worldZ), echantillonnage plus proche.
function Get-GroundHeight([double]$wx, [double]$wz) {
    $ix = Get-Texel $wx
    $iz = Get-Texel $wz
    $off = $HmHeader + ($iz * $HmWidth + $ix) * 2
    $u16 = [BitConverter]::ToUInt16($hmBytes, $off)
    return ($u16 / 65535.0) * $HeightScale
}

# =============================================================================
# 2) Generation des textures procedurales .texr (idempotent : reutilise faubourg)
# =============================================================================
# Ecrit un fichier .texr (magic 0x52584554, w, h, sRGB=1, puis w*h RGBA8) rempli
# d'une couleur de base + bruit deterministe. Effet de bord : ecrit sur disque.
# (Les textures peuvent deja exister depuis le faubourg ; on les reecrit a
# l'identique pour rendre ce script autonome.)
function Write-Texr {
    param(
        [string]$Path, [int]$W, [int]$H,
        [int]$R, [int]$G, [int]$B,
        [int]$Noise       # amplitude +/- du bruit par canal
    )
    $rng = New-Object System.Random(0x7E50 + $R + $G * 3 + $B * 7)
    $hdr = New-Object byte[] 16
    [BitConverter]::GetBytes([uint32]0x52584554).CopyTo($hdr, 0)   # 'TEXR'
    [BitConverter]::GetBytes([uint32]$W).CopyTo($hdr, 4)
    [BitConverter]::GetBytes([uint32]$H).CopyTo($hdr, 8)
    [BitConverter]::GetBytes([uint32]1).CopyTo($hdr, 12)            # sRGB=1
    $px = New-Object byte[] ($W * $H * 4)
    $clamp = { param($v) if ($v -lt 0) {0} elseif ($v -gt 255) {255} else {$v} }
    for ($i = 0; $i -lt ($W * $H); $i++) {
        $n  = $rng.Next(-$Noise, $Noise + 1)
        $o  = $i * 4
        $px[$o]   = [byte](& $clamp ($R + $n))
        $px[$o+1] = [byte](& $clamp ($G + $n))
        $px[$o+2] = [byte](& $clamp ($B + $n))
        $px[$o+3] = 255
    }
    $fs = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create)
    try { $fs.Write($hdr, 0, $hdr.Length); $fs.Write($px, 0, $px.Length) }
    finally { $fs.Close() }
    Write-Host ("[Texr] {0} ({1}x{2}, RGB {3},{4},{5})" -f (Split-Path $Path -Leaf), $W, $H, $R, $G, $B)
}

if (-not (Test-Path $TexDir)) { New-Item -ItemType Directory -Force $TexDir | Out-Null }
$TexSize = 128
Write-Texr -Path (Join-Path $TexDir 'plaster_light.texr') -W $TexSize -H $TexSize -R 210 -G 205 -B 190 -Noise 10
Write-Texr -Path (Join-Path $TexDir 'roof_tile.texr')     -W $TexSize -H $TexSize -R 150 -G 70  -B 45  -Noise 14

# =============================================================================
# 3) Choix des centres de maison (deterministe)
# =============================================================================
# Tire des centres candidats sur une grille jitteree, garde ceux qui sont sur
# le plateau (sol >= GroundMin), peu pentus, espaces de >= MinSpacing.
# Retourne une liste de PSCustomObject {X,Z,Y}.
$rng = New-Object System.Random($Seed)
$centers = New-Object System.Collections.ArrayList

$step = $MinSpacing
$gx = $XMin
while ($gx -le $XMax) {
    $gz = $ZMin
    while ($gz -le $ZMax) {
        $cx = $gx + ($rng.NextDouble() - 0.5) * $step
        $cz = $gz + ($rng.NextDouble() - 0.5) * $step
        if ($cx -ge $XMin -and $cx -le $XMax -and $cz -ge $ZMin -and $cz -le $ZMax) {
            $g = Get-GroundHeight $cx $cz
            if ($g -ge $GroundMin) {
                # Pente : denivele aux 4 coins de l'empreinte.
                $h0 = Get-GroundHeight ($cx-$HalfHouse) ($cz-$HalfHouse)
                $h1 = Get-GroundHeight ($cx+$HalfHouse) ($cz-$HalfHouse)
                $h2 = Get-GroundHeight ($cx-$HalfHouse) ($cz+$HalfHouse)
                $h3 = Get-GroundHeight ($cx+$HalfHouse) ($cz+$HalfHouse)
                $hi = [math]::Max([math]::Max($h0,$h1),[math]::Max($h2,$h3))
                $lo = [math]::Min([math]::Min($h0,$h1),[math]::Min($h2,$h3))
                if (($hi - $lo) -le $MaxSlope) {
                    # Espacement min vs centres deja retenus.
                    $ok = $true
                    foreach ($c in $centers) {
                        $dx = $c.X - $cx; $dz = $c.Z - $cz
                        if (($dx*$dx + $dz*$dz) -lt ($MinSpacing * $MinSpacing)) { $ok = $false; break }
                    }
                    if ($ok) {
                        $baseY = [math]::Round(($g - 0.2), 3)
                        [void]$centers.Add([PSCustomObject]@{ X = [math]::Round($cx,3); Z = [math]::Round($cz,3); Y = $baseY })
                    }
                }
            }
        }
        $gz += $step
    }
    $gx += $step
}

if ($centers.Count -gt $TargetHouses) {
    $centers = $centers[0..($TargetHouses-1)]
}
Write-Host ("[Placement] {0} maison(s) retenue(s) sur le plateau." -f $centers.Count)
if ($centers.Count -eq 0) { throw "Aucun centre valide : verifier la zone / seuil de plateau (GroundMin=$GroundMin)." }

# =============================================================================
# 4) Composition d'une maison (meme gabarit que le faubourg)
# =============================================================================
# Emet les 6 pieces d'UNE maison (dalle, 4 murs dont 1 porte, toit) sous forme
# de hashtables d'entree scenery. Toutes partagent le meme `y` = base_y.
# \param Cx,Cz centre monde (m) ; \param BaseY hauteur de base partagee (m) ;
# \param Yaw orientation maison (deg).
function New-HousePieces {
    param([double]$Cx, [double]$Cz, [double]$BaseY, [double]$Yaw)
    $pieces = New-Object System.Collections.ArrayList
    $roofY  = [math]::Round($BaseY + $WallHeight, 3)
    $cosY = [math]::Cos($Yaw * [math]::PI / 180.0)
    $sinY = [math]::Sin($Yaw * [math]::PI / 180.0)

    # Tourne un offset local (lx,lz) par le yaw maison et translate au centre.
    function _world([double]$lx, [double]$lz) {
        $wx = $Cx + ($lx * $cosY - $lz * $sinY)
        $wz = $Cz + ($lx * $sinY + $lz * $cosY)
        return @([math]::Round($wx,3), [math]::Round($wz,3))
    }

    # Dalle de sol au centre.
    [void]$pieces.Add(@{
        mesh = 'meshes/props/Floor_Brick.gltf'; x = $Cx; z = $Cz; yaw_deg = $Yaw;
        scale = $FloorScale; collision_radius = 0.0; solid = $true; y = $BaseY; albedo = $AlbFloor
    })

    # 4 murs : avant (+Z, porte), arriere (-Z), droite (+X), gauche (-X).
    $defs = @(
        @{ lx = 0.0;          lz =  $HalfHouse; ry =   0.0; door = $true  },
        @{ lx = 0.0;          lz = -$HalfHouse; ry = 180.0; door = $false },
        @{ lx =  $HalfHouse;  lz = 0.0;         ry =  90.0; door = $false },
        @{ lx = -$HalfHouse;  lz = 0.0;         ry = 270.0; door = $false }
    )
    foreach ($d in $defs) {
        $p = _world $d.lx $d.lz
        $mesh = if ($d.door) { 'meshes/props/Wall_Plaster_Door_Flat.gltf' } else { 'meshes/props/Wall_Plaster_Straight.gltf' }
        [void]$pieces.Add(@{
            mesh = $mesh; x = $p[0]; z = $p[1];
            yaw_deg = [math]::Round(($Yaw + $d.ry) % 360.0, 3);
            scale = $WallScale; collision_radius = 0.0; solid = $true; y = $BaseY; albedo = $AlbWall
        })
    }

    # Toit au-dessus, oriente comme la maison.
    [void]$pieces.Add(@{
        mesh = 'meshes/props/Roof_RoundTiles_4x4.gltf'; x = $Cx; z = $Cz; yaw_deg = $Yaw;
        scale = [math]::Round($RoofScale,4); collision_radius = 0.0; solid = $false; y = $roofY; albedo = $AlbRoof
    })

    return $pieces
}

# =============================================================================
# 5) Composition d'une tour / fleche de pierre
# =============================================================================
# Emet les pieces d'une tour : NumRings anneaux de 4 murs brique empiles via `y`
# + 1 toit conique au sommet. Chaque mur d'un anneau est decale de +/-Half le
# long de sa normale et oriente par yaw (0/90/180/270).
# \param Cx,Cz centre monde (m) ; \param Base hauteur de base (m) ;
# \param Half demi-empreinte (m) ; \param WScale scale mur ; \param RScale scale toit ;
# \param NumRings nombre d'anneaux ; \param Solid murs solides (bool).
function New-TowerPieces {
    param(
        [double]$Cx, [double]$Cz, [double]$Base,
        [double]$Half, [double]$WScale, [double]$RScale,
        [int]$NumRings, [bool]$Solid
    )
    $pieces = New-Object System.Collections.ArrayList

    $defs = @(
        @{ lx = 0.0;     lz =  $Half; ry =   0.0 },  # avant  (+Z)
        @{ lx = 0.0;     lz = -$Half; ry = 180.0 },  # arriere (-Z)
        @{ lx =  $Half;  lz = 0.0;    ry =  90.0 },  # droite  (+X)
        @{ lx = -$Half;  lz = 0.0;    ry = 270.0 }   # gauche  (-X)
    )

    for ($r = 0; $r -lt $NumRings; $r++) {
        $ringY = [math]::Round($Base + $r * $BrickHeight, 3)
        foreach ($d in $defs) {
            [void]$pieces.Add(@{
                mesh = 'meshes/props/Wall_UnevenBrick_Straight.gltf';
                x = [math]::Round($Cx + $d.lx, 3); z = [math]::Round($Cz + $d.lz, 3);
                yaw_deg = $d.ry; scale = [math]::Round($WScale,4); collision_radius = 0.0;
                solid = $Solid; y = $ringY; albedo = $AlbStone
            })
        }
    }

    # Toit / cap conique au sommet.
    $roofY = [math]::Round($Base + $NumRings * $BrickHeight, 3)
    [void]$pieces.Add(@{
        mesh = 'meshes/props/Roof_Tower_RoundTiles.gltf';
        x = [math]::Round($Cx, 3); z = [math]::Round($Cz, 3); yaw_deg = 0.0;
        scale = [math]::Round($RScale, 4); collision_radius = 0.0;
        solid = $true; y = $roofY; albedo = $AlbStone
    })

    return $pieces
}

# =============================================================================
# 6) Lecture du count + reecriture du bloc scenery
# =============================================================================
$cfgRaw = [System.IO.File]::ReadAllText($ConfigPath, [System.Text.Encoding]::UTF8)
$cfgObj = $cfgRaw | ConvertFrom-Json     # valide le JSON courant + lit le count
$oldCount = [int]$cfgObj.world.scenery.count
Write-Host ("[Scenery] count existant = {0} (attendu 221)." -f $oldCount)

# Decoupe en lignes (existant en CRLF ; on convertira la sortie en LF).
$lines = $cfgRaw -split "`r`n", 0

# Localise la ligne "scenery": { et la ligne de fin "}," du bloc.
$startIdx = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\s*"scenery"\s*:\s*\{\s*$') { $startIdx = $i; break }
}
if ($startIdx -lt 0) { throw 'Bloc scenery introuvable dans config.json.' }
$endIdx = -1
for ($i = $startIdx + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\s{8}\}\,\s*$') { $endIdx = $i; break }
}
if ($endIdx -lt 0) { throw "Fin du bloc scenery introuvable." }

# Indentation des entrees (12 espaces, conforme a l'existant).
$ind = '            '

# Recolte les lignes d'entree existantes telles quelles (sauf "count") en
# retirant la virgule de continuation (reajoutee uniformement).
$existingEntries = New-Object System.Collections.ArrayList
for ($i = $startIdx + 1; $i -lt $endIdx; $i++) {
    $ln = $lines[$i]
    if ($ln -match '^\s*"count"\s*:') { continue }
    if ($ln.Trim().Length -eq 0) { continue }
    [void]$existingEntries.Add(($ln.TrimEnd() -replace ',\s*$',''))
}

# Serialise une entree en une ligne JSON compacte, ordre de champs stable
# (culture invariante pour le separateur decimal point).
function Format-Entry([int]$idx, [hashtable]$e) {
    $ci = [System.Globalization.CultureInfo]::InvariantCulture
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append($ind).Append('"').Append($idx).Append('": { ')
    [void]$sb.Append('"mesh": "').Append($e.mesh).Append('", ')
    [void]$sb.Append('"x": ').Append([string]::Format($ci,'{0}',$e.x)).Append(', ')
    [void]$sb.Append('"z": ').Append([string]::Format($ci,'{0}',$e.z)).Append(', ')
    [void]$sb.Append('"yaw_deg": ').Append([string]::Format($ci,'{0}',$e.yaw_deg)).Append(', ')
    [void]$sb.Append('"scale": ').Append([string]::Format($ci,'{0}',$e.scale)).Append(', ')
    [void]$sb.Append('"collision_radius": ').Append([string]::Format($ci,'{0}',$e.collision_radius)).Append(', ')
    [void]$sb.Append('"solid": ').Append($(if ($e.solid) {'true'} else {'false'})).Append(', ')
    [void]$sb.Append('"y": ').Append([string]::Format($ci,'{0}',$e.y)).Append(', ')
    [void]$sb.Append('"albedo": "').Append($e.albedo).Append('" }')
    return $sb.ToString()
}

# --- Construit toutes les pieces de la cite ----------------------------------
$cityEntries = New-Object System.Collections.ArrayList
$idx = $oldCount

# 6a) Maisons.
$houseCount = 0
$piecesPerHouse = 0
foreach ($c in $centers) {
    $yaw = [math]::Round(($rng.NextDouble() * 30.0 - 15.0), 2)
    $pieces = New-HousePieces -Cx $c.X -Cz $c.Z -BaseY $c.Y -Yaw $yaw
    if ($piecesPerHouse -eq 0) { $piecesPerHouse = $pieces.Count }
    foreach ($p in $pieces) { [void]$cityEntries.Add((Format-Entry $idx $p)); $idx++ }
    $houseCount++
}

# 6b) Monuments centraux : 3 grandes tours autour du centre + 1 fleche au centre.
# Base partagee = sol au centre du plateau (chaque tour rebase sur son propre sol).
$towerOffsets = @(
    @{ dx = -14.0; dz = -10.0 },   # tour ouest
    @{ dx =  14.0; dz = -10.0 },   # tour est
    @{ dx =   0.0; dz =  16.0 }    # tour sud
)
$towerCount = 0
foreach ($t in $towerOffsets) {
    $tx = $CenterX + $t.dx
    $tz = $CenterZ + $t.dz
    $g  = Get-GroundHeight $tx $tz
    $base = [math]::Round(($g - 0.2), 3)
    $pieces = New-TowerPieces -Cx $tx -Cz $tz -Base $base -Half $HalfTower `
        -WScale $TowerWallScale -RScale $TowerRoofScale -NumRings $TowerRings -Solid $true
    foreach ($p in $pieces) { [void]$cityEntries.Add((Format-Entry $idx $p)); $idx++ }
    $towerCount++
}

# Fleche de cathedrale au centre exact du plateau (la plus haute).
$sg   = Get-GroundHeight $CenterX $CenterZ
$sbase = [math]::Round(($sg - 0.2), 3)
$spirePieces = New-TowerPieces -Cx $CenterX -Cz $CenterZ -Base $sbase -Half $HalfSpire `
    -WScale $SpireWallScale -RScale $SpireRoofScale -NumRings $SpireRings -Solid $true
foreach ($p in $spirePieces) { [void]$cityEntries.Add((Format-Entry $idx $p)); $idx++ }
$spireCount = 1

$newCount = $idx
$cityPieces = $cityEntries.Count
Write-Host ("[Scenery] {0} maisons x {1} pieces + {2} tours + {3} fleche = {4} nouvelles entrees -> count {5}" -f $houseCount, $piecesPerHouse, $towerCount, $spireCount, $cityPieces, $newCount)

# Recompose le bloc complet.
$blockLines = New-Object System.Collections.ArrayList
[void]$blockLines.Add('        "scenery": {')
[void]$blockLines.Add($ind + '"count": ' + $newCount + ',')
foreach ($e in $existingEntries) { [void]$blockLines.Add($e + ',') }
for ($k = 0; $k -lt $cityEntries.Count; $k++) {
    $suffix = if ($k -lt ($cityEntries.Count - 1)) { ',' } else { '' }
    [void]$blockLines.Add($cityEntries[$k] + $suffix)
}
[void]$blockLines.Add('        },')

# Remplace les lignes [startIdx..endIdx] par le nouveau bloc.
$before = if ($startIdx -gt 0) { $lines[0..($startIdx-1)] } else { @() }
$after  = if ($endIdx -lt ($lines.Count-1)) { $lines[($endIdx+1)..($lines.Count-1)] } else { @() }
$allLines = @()
$allLines += $before
$allLines += $blockLines.ToArray()
$allLines += $after

# Reassemble en LF (convention repo) ; ecriture UTF-8 sans BOM (preserve les
# accents des commentaires existants, ex. "M45 — Seuil").
$outText = ($allLines -join "`n") -replace "`r`n", "`n" -replace "`r", "`n"
$enc = New-Object System.Text.UTF8Encoding($false)   # sans BOM
[System.IO.File]::WriteAllText($ConfigPath, $outText, $enc)
Write-Host "[Config] world.scenery reecrit (LF, UTF-8 sans BOM)."

# =============================================================================
# 7) Validation
# =============================================================================
$vTxt = [System.IO.File]::ReadAllText($ConfigPath, [System.Text.Encoding]::UTF8)
$verify = $vTxt | ConvertFrom-Json
$vc = [int]$verify.world.scenery.count
if ($vc -ne $newCount) { throw "Validation: count=$vc attendu $newCount." }
if ($verify.world.scenery.'49'.mesh -notmatch 'Wall_Arch') { throw "Entree 49 alteree." }
if ($verify.world.scenery.'220'.mesh -notmatch 'Roof_Tower_RoundTiles') { throw "Entree 220 alteree." }
$sample = $verify.world.scenery."$oldCount"
if ($null -eq $sample.y -or [string]::IsNullOrEmpty($sample.albedo)) { throw "Echantillon cite sans y/albedo." }
# Integrite des commentaires accentues + LF.
if (-not $vTxt.Contains("M45 $([char]0x2014) Seuil")) { throw "Commentaire M45 Seuil (em-dash) altere." }
if ($vTxt.Contains([char]0x00C3)) { throw "Mojibake A-tilde detecte." }
if ($vTxt.Contains("`r")) { throw "Fins de ligne CR detectees (attendu LF pur)." }
Write-Host ("[Validation] OK : count={0}, entree{1}={2} (y={3}, albedo={4})" -f $vc, $oldCount, $sample.mesh, $sample.y, $sample.albedo)
Write-Host "[Termine] Cite haute generee (1re iteration)."
