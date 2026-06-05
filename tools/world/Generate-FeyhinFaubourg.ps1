<#
.SYNOPSIS
    Genere le "faubourg bas" de Feyhin Lokcthat : un amas de petites maisons
    medievales (pierre/platre) etagees sur la berge EST, descendant vers la
    riviere. PREMIERE iteration (data-gen only) — geometrie correcte et placement
    coherent priorises ; l'apparence fine sera ajustee en jeu.

.DESCRIPTION
    Mecanisme : les maisons sont posees comme des entrees `world.scenery` dans
    config.json (lues par le client, sans zone_builder). Chaque piece d'une maison
    partage une meme hauteur de base `y` (echantillonnee dans le heightmap au centre
    de la maison) pour que toutes les pieces s'alignent sur la pente au lieu de
    s'accrocher chacune a sa propre hauteur de terrain.

    Le script :
      1. Lit le heightmap SP1 (terrain_height.r16h, format HAMP).
      2. Choisit ~30-40 centres de maison valides dans la zone faubourg
         (X dans [+30,+160], Z dans [+200,+320]) ou le sol est entre ~62 et ~150 m
         (terre seche sur la pente), espaces d'au moins ~7 m, sans chevauchement.
      3. Compose chaque maison (boite 4x4 m) : 1 dalle de sol, 4 murs (un mur avant
         a porte), 1 toit en tuiles. Calcule (x,z), yaw_deg, scale, scale_y et y de
         chaque piece a partir du centre + dims extraites des meshes.
      4. Genere les textures procedurales .texr (platre clair, tuile terracotta).
      5. Reecrit UNIQUEMENT le bloc world.scenery de config.json en conservant
         intactes les 50 entrees du pont (indices 0-49) et le style de fin de ligne
         existant du fichier (CRLF), nouvelles entrees a partir de l'index 50,
         count mis a jour.

    NE COMMET RIEN (le controleur gere git).

.NOTES
    PowerShell 5.1. Generation de donnees uniquement.
#>

[CmdletBinding()]
param(
    # Racine du worktree (contient config.json + game/).
    [string]$Root = "D:\Users\thedj\git\LCDLLN-wt-sp3",
    # Graine deterministe pour le placement.
    [int]$Seed = 1453
)

$ErrorActionPreference = 'Stop'

# --- Chemins -----------------------------------------------------------------
$ConfigPath  = Join-Path $Root 'config.json'
$HeightPath  = Join-Path $Root 'game\data\zones\feyhin_lokcthat\terrain_height.r16h'
$TexDir      = Join-Path $Root 'game\data\meshes\props\textures'

# --- Constantes monde / heightmap (SP1 Feyhin) -------------------------------
$WorldSize   = 1536.0            # cote du carre monde, metres
$Origin      = -768.0            # coin -X/-Z du heightmap, metres
$HmWidth     = 1025              # texels par cote (lu et verifie ci-dessous)
$HmHeader    = 12                # uint32 magic + uint32 width + uint32 height
$HeightScale = 512.0            # hauteur metres = u16/65535*512
$WaterLevel  = 60.0             # niveau d'eau, metres

# --- Zone faubourg (berge EST) ----------------------------------------------
$XMin = 30.0;  $XMax = 160.0
$ZMin = 200.0; $ZMax = 320.0
$GroundMin = 60.5              # sous = eau (niveau y=60) -> rejet ; 60.5 capte la berge basse
$GroundMax = 150.0            # au-dessus = crag trop raide -> rejet
$MaxSlope  = 30.0            # denivele max (m) sur l'empreinte : le faubourg est etage sur la pente
$MinSpacing = 7.0           # distance min centre-a-centre, metres
$TargetHouses = 38         # cible (la bande de terre seche valide peut en livrer moins)

# --- Dims extraites des meshes (AABB local, m) -------------------------------
# Wall_Plaster_Straight / _Door_Flat : X[-1,1]=2 m large, Y[0,3.12] haut, Z~0.4 ep.
# Floor_Brick : X[-1,1] Z[-1,1] = 2x2 m, dalle plate a Y~0.
# Roof_RoundTiles_4x4 : X[-2.57,2.57]~5.13 m, Z~5.5 m, base couvre ~5.1x5.5 m.
$WallWidth   = 2.0            # largeur d'un module mur (X local)
$WallHeight  = 3.12          # hauteur mur (Y local max)
$FloorHalf   = 1.0           # demi-cote dalle (X/Z local) -> 2x2 m a scale 1
$RoofBaseX   = 5.1352        # largeur base toit (X) a scale 1

# Empreinte maison : 4x4 m (mur scale 2 -> 4 m large ; dalle scale 2 -> 4x4 m).
$HouseSize   = 4.0
$WallScale   = $HouseSize / $WallWidth        # 2.0 -> mur de 4 m
$FloorScale  = $HouseSize / ($FloorHalf*2)    # 2.0 -> dalle 4x4 m
$RoofScale   = $HouseSize / $RoofBaseX        # ~0.779 -> toit couvrant ~4 m
$HalfHouse   = $HouseSize / 2.0               # 2.0 m, offset centre->mur

# --- Albedos -----------------------------------------------------------------
$AlbWall  = 'meshes/props/textures/plaster_light.texr'
$AlbRoof  = 'meshes/props/textures/roof_tile.texr'
$AlbFloor = 'meshes/props/textures/stone_dark.texr'   # reutilise l'existant

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
# 2) Generation des textures procedurales .texr
# =============================================================================
# Ecrit un fichier .texr (magic 0x52584554, w, h, sRGB=1, puis w*h RGBA8) rempli
# d'une couleur de base + bruit subtil deterministe. Effet de bord : ecrit sur disque.
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
# terre seche (sol dans [GroundMin,GroundMax]), pas trop pentus, et espaces de
# >= MinSpacing. Retourne une liste de PSCustomObject {X,Z,Y}.
$rng = New-Object System.Random($Seed)
$centers = New-Object System.Collections.ArrayList

# Grille de candidats : pas = MinSpacing pour densifier dans la bande etroite de
# terre seche (le rejet espacement/pente elimine ensuite les trop proches).
$step = $MinSpacing
$gx = $XMin
while ($gx -le $XMax) {
    $gz = $ZMin
    while ($gz -le $ZMax) {
        $cx = $gx + ($rng.NextDouble() - 0.5) * $step
        $cz = $gz + ($rng.NextDouble() - 0.5) * $step
        if ($cx -ge $XMin -and $cx -le $XMax -and $cz -ge $ZMin -and $cz -le $ZMax) {
            $g = Get-GroundHeight $cx $cz
            if ($g -ge $GroundMin -and $g -le $GroundMax) {
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
                        # base_y : sol au centre, legerement enfonce pour eviter le flottement.
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

# Limite a la cible (garde les premiers, deja repartis sur la grille).
if ($centers.Count -gt $TargetHouses) {
    $centers = $centers[0..($TargetHouses-1)]
}
Write-Host ("[Placement] {0} maison(s) retenue(s) dans la zone faubourg." -f $centers.Count)
if ($centers.Count -eq 0) { throw "Aucun centre valide : verifier la zone / les seuils de hauteur." }

# =============================================================================
# 4) Composition des pieces d'une maison
# =============================================================================
# Emet les pieces d'UNE maison (dalle, 4 murs, toit) sous forme de hashtables
# d'entree scenery. Toutes partagent le meme `y` = base_y du centre.
# yaw_deg du mur : un mur a yaw=0 s'etend sur l'axe X, plaque a +Z ; on translate
# le centre du mur de +/-HalfHouse le long de la normale et on oriente par yaw.
function New-HousePieces {
    param([double]$Cx, [double]$Cz, [double]$BaseY, [double]$Yaw)
    $pieces = New-Object System.Collections.ArrayList
    $roofY  = [math]::Round($BaseY + $WallHeight, 3)
    $cosY = [math]::Cos($Yaw * [math]::PI / 180.0)
    $sinY = [math]::Sin($Yaw * [math]::PI / 180.0)

    # Helper : tourne un offset local (lx,lz) par le yaw maison et translate au centre.
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
    # Mur de face : centre decale de +HalfHouse en Z local, yaw maison + 0.
    $defs = @(
        @{ lx = 0.0;          lz =  $HalfHouse; ry =   0.0; door = $true  },  # avant (porte)
        @{ lx = 0.0;          lz = -$HalfHouse; ry = 180.0; door = $false },  # arriere
        @{ lx =  $HalfHouse;  lz = 0.0;         ry =  90.0; door = $false },  # droite
        @{ lx = -$HalfHouse;  lz = 0.0;         ry = 270.0; door = $false }   # gauche
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
# 5) Generation de toutes les entrees + reecriture du bloc scenery
# =============================================================================
# Lit le count actuel, garde les entrees existantes 0..count-1 (le pont) telles
# quelles, ajoute les pieces de maison a partir de l'index count, met a jour count.
$cfgRaw = [System.IO.File]::ReadAllText($ConfigPath, [System.Text.Encoding]::UTF8)
$cfgObj = $cfgRaw | ConvertFrom-Json     # valide le JSON courant + lit le count
$oldCount = [int]$cfgObj.world.scenery.count
Write-Host ("[Scenery] count existant = {0} (pont attendu = 50)." -f $oldCount)

# Construit le texte des entrees existantes a l'identique en relisant les lignes
# du bloc (pour ne PAS reformater les 50 entrees du pont).
$lines = $cfgRaw -split "`r`n", 0
# Localise la ligne "scenery": { et la ligne de fin "}," du bloc.
$startIdx = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\s*"scenery"\s*:\s*\{\s*$') { $startIdx = $i; break }
}
if ($startIdx -lt 0) { throw 'Bloc scenery introuvable dans config.json.' }
# Fin du bloc : premiere ligne suivante composee uniquement de "}," (8 espaces).
$endIdx = -1
for ($i = $startIdx + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^\s{8}\}\,\s*$') { $endIdx = $i; break }
}
if ($endIdx -lt 0) { throw "Fin du bloc scenery introuvable." }

# Indentation des entrees (12 espaces, conforme a l'existant).
$ind = '            '

# Recolte les lignes d'entree existantes (indices count) : tout entre la ligne
# "count" et la ligne de fermeture, en conservant l'ordre/format d'origine, sauf
# la derniere entree qui doit reprendre une virgule de continuation.
$existingEntries = New-Object System.Collections.ArrayList
for ($i = $startIdx + 1; $i -lt $endIdx; $i++) {
    $ln = $lines[$i]
    if ($ln -match '^\s*"count"\s*:') { continue }   # on reecrit count nous-memes
    if ($ln.Trim().Length -eq 0) { continue }
    # Retire toute virgule de continuation : on la reajoutera uniformement.
    [void]$existingEntries.Add(($ln.TrimEnd() -replace ',\s*$',''))
}

# Serialise une entree maison en une ligne JSON compacte, ordre de champs stable.
function Format-Entry([int]$idx, [hashtable]$e) {
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append($ind).Append('"').Append($idx).Append('": { ')
    [void]$sb.Append('"mesh": "').Append($e.mesh).Append('", ')
    [void]$sb.Append('"x": ').Append([string]::Format([System.Globalization.CultureInfo]::InvariantCulture,'{0}',$e.x)).Append(', ')
    [void]$sb.Append('"z": ').Append([string]::Format([System.Globalization.CultureInfo]::InvariantCulture,'{0}',$e.z)).Append(', ')
    [void]$sb.Append('"yaw_deg": ').Append([string]::Format([System.Globalization.CultureInfo]::InvariantCulture,'{0}',$e.yaw_deg)).Append(', ')
    [void]$sb.Append('"scale": ').Append([string]::Format([System.Globalization.CultureInfo]::InvariantCulture,'{0}',$e.scale)).Append(', ')
    [void]$sb.Append('"collision_radius": ').Append([string]::Format([System.Globalization.CultureInfo]::InvariantCulture,'{0}',$e.collision_radius)).Append(', ')
    [void]$sb.Append('"solid": ').Append($(if ($e.solid) {'true'} else {'false'})).Append(', ')
    [void]$sb.Append('"y": ').Append([string]::Format([System.Globalization.CultureInfo]::InvariantCulture,'{0}',$e.y)).Append(', ')
    [void]$sb.Append('"albedo": "').Append($e.albedo).Append('" }')
    return $sb.ToString()
}

# Construit toutes les pieces de toutes les maisons.
$houseEntries = New-Object System.Collections.ArrayList
$idx = $oldCount
$piecesPerHouse = 0
foreach ($c in $centers) {
    $yaw = [math]::Round(($rng.NextDouble() * 30.0 - 15.0), 2)   # petit yaw aleatoire +/-15 deg
    $pieces = New-HousePieces -Cx $c.X -Cz $c.Z -BaseY $c.Y -Yaw $yaw
    if ($piecesPerHouse -eq 0) { $piecesPerHouse = $pieces.Count }
    foreach ($p in $pieces) {
        [void]$houseEntries.Add((Format-Entry $idx $p))
        $idx++
    }
}
$newCount = $idx
Write-Host ("[Scenery] {0} maisons x {1} pieces = {2} nouvelles entrees -> count {3}" -f $centers.Count, $piecesPerHouse, $houseEntries.Count, $newCount)

# Recompose le bloc complet.
$blockLines = New-Object System.Collections.ArrayList
[void]$blockLines.Add('        "scenery": {')
[void]$blockLines.Add($ind + '"count": ' + $newCount + ',')
foreach ($e in $existingEntries) { [void]$blockLines.Add($e + ',') }
for ($k = 0; $k -lt $houseEntries.Count; $k++) {
    $suffix = if ($k -lt ($houseEntries.Count - 1)) { ',' } else { '' }
    [void]$blockLines.Add($houseEntries[$k] + $suffix)
}
[void]$blockLines.Add('        },')

# Remplace les lignes [startIdx..endIdx] par le nouveau bloc.
$before = if ($startIdx -gt 0) { $lines[0..($startIdx-1)] } else { @() }
$after  = if ($endIdx -lt ($lines.Count-1)) { $lines[($endIdx+1)..($lines.Count-1)] } else { @() }
$allLines = @()
$allLines += $before
$allLines += $blockLines.ToArray()
$allLines += $after

# Reassemble en LF (convention repo) ; lecture/ecriture UTF-8 sans BOM (preserve les
# accents des commentaires existants).
$outText = ($allLines -join "`n") -replace "`r`n", "`n" -replace "`r", "`n"
$enc = New-Object System.Text.UTF8Encoding($false)   # sans BOM
[System.IO.File]::WriteAllText($ConfigPath, $outText, $enc)
Write-Host "[Config] world.scenery reecrit (LF, UTF-8 sans BOM)."

# =============================================================================
# 6) Validation
# =============================================================================
$verify = (Get-Content $ConfigPath -Raw) | ConvertFrom-Json   # reparse complet
$vc = [int]$verify.world.scenery.count
if ($vc -ne $newCount) { throw "Validation: count=$vc attendu $newCount." }
# Premieres 50 entrees toujours le pont ?
if ($verify.world.scenery.'0'.mesh -notmatch 'Floor_UnevenBrick') { throw "Entree 0 alteree." }
if ($verify.world.scenery.'49'.mesh -notmatch 'Wall_Arch') { throw "Entree 49 alteree." }
# Echantillon maison.
$sample = $verify.world.scenery."$oldCount"
if ($null -eq $sample.y -or [string]::IsNullOrEmpty($sample.albedo)) { throw "Echantillon maison sans y/albedo." }
Write-Host ("[Validation] OK : count={0}, entree50={1} (y={2}, albedo={3})" -f $vc, $sample.mesh, $sample.y, $sample.albedo)
Write-Host "[Termine] Faubourg bas genere (1re iteration)."
