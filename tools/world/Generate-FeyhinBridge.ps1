<#
.SYNOPSIS
    Genere le pont de pierre de Feyhin Lokcthat (SP2b) : calcule les entrees
    world.scenery (dalles de tablier + arches) et les ecrit dans config.json.

.DESCRIPTION
    Pont droit a Z=300 traversant la riviere d'ouest (x=-135) en est (x=+90),
    soit 225 m. Tablier continu de dalles Floor_UnevenBrick a y=64, et 10 arches
    Wall_Arch posees pied au niveau de l'eau (y=60). Textures de pierre sombre
    appliquees via le champ optionnel `albedo`.

    Le script lit config.json en texte, remplace le bloc `"scenery": {...}` (vide)
    par le bloc peuple, et reecrit en UTF-8 LF sans BOM. Le reste du fichier n'est
    pas reformate.

    Premiere iteration : l'echelle/apparence sera affinee apres revue en jeu.
    NOTE: l'arche Wall_Arch a un ratio L/H de 2/3 ; a echelle uniforme, une
    largeur de ~21 m imposerait une hauteur de ~31.5 m. On applique donc un
    `scale_y` independant (1.5) pour obtenir des arches basses ~4.5 m de haut,
    en conservant le `scale` uniforme (~10.5) pour largeur et profondeur.
#>

# --- Dimensions extraites des AABB locales des meshes (gltf accessors min/max) ---
# Floor_UnevenBrick : 2 x 0.02 x 2 m (dalle plate carree).
# Wall_Arch         : 2 (large) x 3 (haut) x 0.064 (epaisseur) m ; pied a y_local~0.
$floorSizeX = 2.0
$floorSizeZ = 2.0
$archSizeX  = 2.0   # largeur (X local) -> portee de l'ouverture
$archSizeY  = 3.0   # hauteur (Y local)

# --- Geometrie du pont ---
$bridgeZ      = 300.0
$xWest        = -135.0
$xEast        = 90.0
$bridgeLen    = $xEast - $xWest          # 225 m
$deckY        = 64.0
$waterY       = 60.0

$albedo = "meshes/props/textures/stone_dark.texr"

# --- Tablier : dalles Floor_UnevenBrick a echelle uniforme ---
# Largeur de tablier visee ~5.75 m -> scale = 5.75 / 2 = 2.875 ; chaque dalle
# couvre alors 5.75 x 5.75 m. On pave la longueur du pont.
$deckWidth   = 5.75
$deckScale   = [math]::Round($deckWidth / $floorSizeX, 4)   # 2.875
$deckTile    = $floorSizeX * $deckScale                     # 5.75 m le long de l'axe
$deckCount   = [int][math]::Ceiling($bridgeLen / $deckTile) # 40
$deckYaw     = 0.0   # dalle carree symetrique : orientation indifferente

# --- Arches : Wall_Arch a echelle uniforme ---
# 10 arches sur 225 m -> 22.5 m par travee. Echelle pour largeur ~21 m.
$archCount   = 10
$archSpan    = $bridgeLen / $archCount                      # 22.5 m
$archWidthM  = 21.0
$archScale   = [math]::Round($archWidthM / $archSizeX, 4)   # 10.5 (largeur + profondeur)
# Echelle Y independante : arche basse ~4.5 m de haut (hauteur locale 3 m * 1.5).
$archScaleY  = 1.5
$archHeightM = $archSizeY * $archScaleY                     # 4.5 m
$archY       = $waterY   # pied de l'arche au niveau de l'eau
$archYaw     = 90.0      # ouverture face a l'ecoulement (broadside vue du sud)

# --- Construction des entrees ---
$entries = New-Object System.Collections.Generic.List[string]
$idx = 0

function New-Entry($mesh, $x, $z, $yaw, $scale, $y, $scaleY = -1.0) {
    $ic = [System.Globalization.CultureInfo]::InvariantCulture
    # Champ scale_y optionnel : insere seulement si > 0 (sinon echelle uniforme).
    $scaleYField = ''
    if ($scaleY -gt 0.0) {
        $scaleYField = ', "scale_y": ' + [string]::Format($ic, "{0:0.####}", $scaleY)
    }
    $fmt = '            "{0}": {{ "mesh": "{1}", "x": {2}, "z": {3}, "yaw_deg": {4}, "scale": {5}, "collision_radius": 0.0, "solid": true, "y": {6}{7}, "albedo": "{8}" }}'
    return ($fmt -f $idx, $mesh,
        ([string]::Format($ic, "{0:0.###}", $x)),
        ([string]::Format($ic, "{0:0.###}", $z)),
        ([string]::Format($ic, "{0:0.###}", $yaw)),
        ([string]::Format($ic, "{0:0.####}", $scale)),
        ([string]::Format($ic, "{0:0.###}", $y)),
        $scaleYField,
        $albedo)
}

# Dalles du tablier (centrees, pavage continu).
for ($i = 0; $i -lt $deckCount; $i++) {
    $x = $xWest + $deckTile / 2.0 + $i * $deckTile
    $entries.Add( (New-Entry "meshes/props/Floor_UnevenBrick.gltf" $x $bridgeZ $deckYaw $deckScale $deckY) )
    $idx++
}

# Arches (une par travee).
for ($i = 0; $i -lt $archCount; $i++) {
    $x = $xWest + $archSpan / 2.0 + $i * $archSpan
    $entries.Add( (New-Entry "meshes/props/Wall_Arch.gltf" $x $bridgeZ $archYaw $archScale $archY $archScaleY) )
    $idx++
}

$count = $entries.Count

# --- Bloc scenery JSON peuple (indentation 8 espaces pour la cle) ---
$nl = "`n"
$sb = New-Object System.Text.StringBuilder
[void]$sb.Append('        "scenery": {')
[void]$sb.Append($nl)
[void]$sb.Append('            "count": ' + $count + ',')
[void]$sb.Append($nl)
[void]$sb.Append( ($entries -join (',' + $nl)) )
[void]$sb.Append($nl)
[void]$sb.Append('        }')
$newBlock = $sb.ToString()

# --- Edition de config.json (remplacement du bloc scenery vide) ---
$cfgPath = Join-Path $PSScriptRoot "..\..\config.json"
$cfgPath = [System.IO.Path]::GetFullPath($cfgPath)
$raw = [System.IO.File]::ReadAllText($cfgPath)

# Le bloc cible est `"scenery": { ... }` : soit vide (1er passage), soit deja
# peuple (re-generation idempotente). On capture depuis la cle scenery indentee
# jusqu'a son accolade fermante propre, indentee a 8 espaces sur sa ligne (les
# entrees internes sont sur une seule ligne, donc sans `}` isole indente ainsi).
$pattern = '(?s)[ \t]*"scenery"\s*:\s*\{.*?\n        \}'
if ($raw -notmatch $pattern) {
    # Repli : bloc vide compact (avant tout premier peuplement).
    $pattern = '(?s)[ \t]*"scenery"\s*:\s*\{\s*\}'
    if ($raw -notmatch $pattern) {
        throw "Bloc 'scenery' introuvable dans config.json."
    }
}
# Replace via un MatchEvaluator pour ne pas interpreter les $-substitutions
# ni les accolades du bloc JSON de remplacement.
$rx = [System.Text.RegularExpressions.Regex]::new($pattern)
$raw = $rx.Replace($raw, [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $newBlock }, 1)

# Normalisation LF + UTF-8 sans BOM.
$raw = $raw -replace "`r`n", "`n"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($cfgPath, $raw, $utf8NoBom)

# --- Rapport ---
Write-Output "Floor_UnevenBrick: $floorSizeX x ?? x $floorSizeZ m ; deckScale=$deckScale tile=$deckTile m count=$deckCount"
Write-Output "Wall_Arch: $archSizeX (W) x $archSizeY (H) m ; archScale=$archScale scaleY=$archScaleY -> width=$archWidthM m height=$archHeightM m ; span=$archSpan m count=$archCount"
Write-Output "Total scenery entries: $count (deck=$deckCount + arches=$archCount)"
Write-Output "config.json mis a jour : $cfgPath"
