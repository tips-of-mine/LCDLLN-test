<#
.SYNOPSIS
    Genere le "monument isole" de Feyhin Lokcthat : une haute tour de pierre
    autoportante (~25 m) dressee sur l'eperon rocheux a l'ecart du faubourg.
    PREMIERE iteration (data-gen only) — geometrie correcte et empilement
    coherent priorises ; l'apparence fine sera ajustee en jeu.

.DESCRIPTION
    Mecanisme : la tour est posee comme un ensemble d'entrees `world.scenery`
    dans config.json (lues par le client, sans zone_builder). Chaque piece
    (mur, toit) recoit un `y` explicite : la base est echantillonnee dans le
    heightmap au sommet de l'eperon, puis chaque anneau est empile a
    `y = base_y + ring_index * wallHeight` pour batir la tour vers le haut.

    Le script :
      1. Lit le heightmap SP1 (terrain_height.r16h, format HAMP).
      2. Echantillonne base_y = sol au point monde (+55, +345) (sommet eperon).
      3. Empile 8 anneaux de 4 murs (UnevenBrick, pierre) autour d'une empreinte
         carree ~5x5 m (yaw 0/90/180/270, decales de la demi-empreinte le long
         de la normale). Hauteur d'anneau = hauteur de mur (~3.12 m) -> ~25 m.
      4. Coiffe la tour d'un toit conique (Roof_Tower_RoundTiles) au sommet
         (y = base_y + 8*wallHeight), mis a l'echelle pour couvrir l'empreinte.
      5. Reecrit UNIQUEMENT le bloc world.scenery de config.json en conservant
         intactes les 188 entrees existantes (indices 0-187 : pont + faubourg),
         nouvelles entrees a partir de l'index 188, count mis a jour. Sortie en
         LF, UTF-8 sans BOM (preserve les accents des commentaires existants).

    NE COMMET RIEN (le controleur gere git).

.NOTES
    PowerShell 5.1. Generation de donnees uniquement.
    Dims extraites des meshes (AABB local, m) :
      Wall_UnevenBrick_Straight : X[-1,1]=2 m large, Y[0,3.1227] haut, Z~0.4 ep.
      Floor_Brick               : X[-1,1] Z[-1,1] = 2x2 m, dalle plate a Y~0.
      Roof_Tower_RoundTiles     : X/Z [-2.825,2.825] ~5.65 m, Y[-0.57,6.79] cone.
#>

[CmdletBinding()]
param(
    # Racine du worktree (contient config.json + game/).
    [string]$Root = "D:\Users\thedj\git\LCDLLN-wt-sp4"
)

$ErrorActionPreference = 'Stop'

# --- Chemins -----------------------------------------------------------------
$ConfigPath = Join-Path $Root 'config.json'
$HeightPath = Join-Path $Root 'game\data\zones\feyhin_lokcthat\terrain_height.r16h'

# --- Constantes monde / heightmap (SP1 Feyhin) -------------------------------
$WorldSize   = 1536.0            # cote du carre monde, metres
$Origin      = -768.0            # coin -X/-Z du heightmap, metres
$HmWidth     = 1025              # texels par cote
$HmHeader    = 12                # uint32 magic + uint32 width + uint32 height
$HeightScale = 512.0             # hauteur metres = u16/65535*512

# --- Position du monument ----------------------------------------------------
$MonX = 55.0    # eperon isole, a l'ecart du faubourg
$MonZ = 345.0

# --- Dims extraites des meshes (AABB local, m) -------------------------------
$WallWidth  = 2.0        # largeur d'un module mur (X local : [-1,1])
$WallHeight = 3.1227     # hauteur geometrique du mur (Y max acc[0]) -> pas d'empilement
$RoofBaseX  = 5.65052    # largeur base toit conique (X : 2*2.825263) a scale 1

# --- Empreinte tour : carre ~5x5 m -------------------------------------------
$TowerSize = 5.0
$WallScale = $TowerSize / $WallWidth   # 2.5 -> mur de 5 m de large
$RoofScale = $TowerSize / $RoofBaseX   # ~0.885 -> cone couvrant ~5 m
$HalfTower = $TowerSize / 2.0          # 2.5 m, offset centre->mur le long de la normale
$NumRings  = 8                         # 8 anneaux x ~3.12 m -> ~25 m

# --- Albedo (pierre sombre, existe deja dans le repo) ------------------------
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

# Base de la tour : sol au point monument, legerement enfonce pour eviter le
# flottement de l'anneau 0.
$ground = Get-GroundHeight $MonX $MonZ
$BaseY  = [math]::Round(($ground - 0.2), 3)
Write-Host ("[Placement] sol a ({0},{1}) = {2:F3} m -> base_y = {3}" -f $MonX, $MonZ, $ground, $BaseY)

# =============================================================================
# 2) Composition des pieces de la tour
# =============================================================================
# Emet les pieces de la tour (8 anneaux de 4 murs + 1 toit conique) sous forme
# de hashtables d'entree scenery. Chaque mur d'un anneau est decale de
# +/-HalfTower le long de sa normale et oriente par yaw (0/90/180/270).
# 4 murs par anneau : avant (+Z, yaw 0), arriere (-Z, yaw 180), droite (+X,
# yaw 90), gauche (-X, yaw 270).
function New-TowerPieces {
    param([double]$Cx, [double]$Cz, [double]$Base)
    $pieces = New-Object System.Collections.ArrayList

    # Definitions des 4 murs autour de l'empreinte carree (offset + yaw).
    $defs = @(
        @{ lx = 0.0;          lz =  $HalfTower; ry =   0.0 },  # avant  (+Z)
        @{ lx = 0.0;          lz = -$HalfTower; ry = 180.0 },  # arriere (-Z)
        @{ lx =  $HalfTower;  lz = 0.0;         ry =  90.0 },  # droite  (+X)
        @{ lx = -$HalfTower;  lz = 0.0;         ry = 270.0 }   # gauche  (-X)
    )

    for ($r = 0; $r -lt $NumRings; $r++) {
        $ringY = [math]::Round($Base + $r * $WallHeight, 3)
        foreach ($d in $defs) {
            [void]$pieces.Add(@{
                mesh = 'meshes/props/Wall_UnevenBrick_Straight.gltf';
                x = [math]::Round($Cx + $d.lx, 3); z = [math]::Round($Cz + $d.lz, 3);
                yaw_deg = $d.ry; scale = $WallScale; collision_radius = 0.0;
                solid = $true; y = $ringY; albedo = $AlbStone
            })
        }
    }

    # Toit conique au sommet (au-dessus du dernier anneau).
    $roofY = [math]::Round($Base + $NumRings * $WallHeight, 3)
    [void]$pieces.Add(@{
        mesh = 'meshes/props/Roof_Tower_RoundTiles.gltf';
        x = [math]::Round($Cx, 3); z = [math]::Round($Cz, 3); yaw_deg = 0.0;
        scale = [math]::Round($RoofScale, 4); collision_radius = 0.0;
        solid = $true; y = $roofY; albedo = $AlbStone
    })

    return $pieces
}

# =============================================================================
# 3) Lecture du count + reecriture du bloc scenery
# =============================================================================
# Lit le count actuel, garde les entrees existantes 0..count-1 telles quelles,
# ajoute les pieces de la tour a partir de l'index count, met a jour count.
$cfgRaw = [System.IO.File]::ReadAllText($ConfigPath, [System.Text.Encoding]::UTF8)
$cfgObj = $cfgRaw | ConvertFrom-Json     # valide le JSON courant + lit le count
$oldCount = [int]$cfgObj.world.scenery.count
Write-Host ("[Scenery] count existant = {0}." -f $oldCount)

# Decoupe en lignes (l'existant est en CRLF ; on convertira la sortie en LF).
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

# Recolte les lignes d'entree existantes telles quelles (sauf "count" qu'on
# reecrit) en retirant la virgule de continuation (reajoutee uniformement).
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

# Construit toutes les pieces de la tour.
$towerPieces = New-TowerPieces -Cx $MonX -Cz $MonZ -Base $BaseY
$newEntries = New-Object System.Collections.ArrayList
$idx = $oldCount
foreach ($p in $towerPieces) {
    [void]$newEntries.Add((Format-Entry $idx $p))
    $idx++
}
$newCount = $idx
Write-Host ("[Scenery] {0} pieces (8 anneaux x 4 murs + 1 toit) -> count {1}" -f $newEntries.Count, $newCount)

# Recompose le bloc complet.
$blockLines = New-Object System.Collections.ArrayList
[void]$blockLines.Add('        "scenery": {')
[void]$blockLines.Add($ind + '"count": ' + $newCount + ',')
foreach ($e in $existingEntries) { [void]$blockLines.Add($e + ',') }
for ($k = 0; $k -lt $newEntries.Count; $k++) {
    $suffix = if ($k -lt ($newEntries.Count - 1)) { ',' } else { '' }
    [void]$blockLines.Add($newEntries[$k] + $suffix)
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
# 4) Validation
# =============================================================================
$vTxt = [System.IO.File]::ReadAllText($ConfigPath, [System.Text.Encoding]::UTF8)
$verify = $vTxt | ConvertFrom-Json
$vc = [int]$verify.world.scenery.count
if ($vc -ne $newCount) { throw "Validation: count=$vc attendu $newCount." }
if ($verify.world.scenery.'49'.mesh -notmatch 'Wall_Arch') { throw "Entree 49 alteree." }
if ($verify.world.scenery.'187'.mesh -notmatch 'Roof_RoundTiles_4x4') { throw "Entree 187 alteree." }
$sample = $verify.world.scenery."$oldCount"
if ($null -eq $sample.y -or [string]::IsNullOrEmpty($sample.albedo)) { throw "Echantillon monument sans y/albedo." }
if ($sample.albedo -notmatch 'stone_dark') { throw "Echantillon monument sans albedo pierre." }
# Integrite des commentaires accentues + LF.
if (-not $vTxt.Contains("M45 $([char]0x2014) Seuil")) { throw "Commentaire M45 Seuil (em-dash) altere." }
if ($vTxt.Contains([char]0x00C3)) { throw "Mojibake A-tilde detecte." }
if ($vTxt.Contains("`r")) { throw "Fins de ligne CR detectees (attendu LF pur)." }
Write-Host ("[Validation] OK : count={0}, entree{1}={2} (y={3}, albedo={4})" -f $vc, $oldCount, $sample.mesh, $sample.y, $sample.albedo)
Write-Host "[Termine] Monument isole genere (1re iteration)."
