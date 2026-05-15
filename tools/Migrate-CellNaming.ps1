<#
.SYNOPSIS
    Migre la nomenclature des cellules du JSON lune-noire-data vers un système
    de coordonnées cardinales extensible, ancré sur l'Oracle (R190C200).

.DESCRIPTION
    Pour chaque entrée du tableau `cells` du JSON :
      - legacy_id  = ancien R###C###                     (préservé pour rétrocompat)
      - id         = N###_E### / N###_W### / S###_E### …  (identifiant cardinal compact,
                                                           extensible dans les 4 directions)
      - name       = cell_N###_E### …                     (format humain-lisible = nom de dossier)
      - tous les autres champs (region_id, terrain_type, has_lake, lake_count,
        has_river, river_count, has_mountain_range, river_direction, connects_to,
        creature_ids, visual_elements, notes, coord_x, coord_y, …) sont préservés
        à l'identique.

    Le script :
      1. Lit le JSON source
      2. Construit le mapping R###C### -> {id cardinal, name} avec origine R190C200
      3. Met à jour chaque cellule du tableau `cells`
      4. (Optionnel, -UpdateReferences) Met à jour les références R###C###
         dans le reste du JSON (ex : villes[*].cell)
      5. Ajoute meta.origin (bloc canonique référençant l'Oracle)
      6. Écrit le JSON enrichi (.bak automatique) + MIGRATION_MAP.json
      7. (Optionnel) Crée/renomme les dossiers de zones sur disque

.PARAMETER JsonPath
    Chemin vers le JSON source (ex : lune-noire-data-v19.json).

.PARAMETER OutputJsonPath
    Chemin de sortie. Par défaut : écrase le source (un .bak est créé).

.PARAMETER MigrationMapPath
    Chemin de sortie pour MIGRATION_MAP.json. Par défaut : à côté du JSON source.

.PARAMETER OriginCellId
    Cellule origine au format R###C###. Par défaut : R190C200 (Oracle).

.PARAMETER UpdateReferences
    Met également à jour toutes les chaînes de caractères R###C### trouvées
    AILLEURS dans le JSON (ex : villes[*].cell) vers le nouveau id cardinal.

.PARAMETER ZonesRoot
    Racine des dossiers de cellules (zone_N/ actuels, futurs cell_*).

.PARAMETER CreateCanonicalFolders
    Crée un dossier vide cell_N###_E###/ pour chaque cellule du JSON.

.PARAMETER ZoneMappingCsv
    CSV (colonnes : zone_name,cell_legacy_id) pour renommer zone_N/ -> cell_N###_E###/.

.PARAMETER RenameZones
    Effectue le renommage d'après -ZoneMappingCsv.

.PARAMETER DryRun
    N'écrit/ne renomme rien ; affiche uniquement ce qui serait fait.

.EXAMPLE
    # Aperçu uniquement
    .\Migrate-CellNaming.ps1 -JsonPath .\lune-noire-data-v19.json -DryRun

.EXAMPLE
    # Migration complète du JSON (cellules + références dans villes etc.)
    .\Migrate-CellNaming.ps1 -JsonPath .\lune-noire-data-v19.json -UpdateReferences

.EXAMPLE
    # JSON + création des dossiers canoniques vides
    .\Migrate-CellNaming.ps1 -JsonPath .\lune-noire-data-v19.json `
                            -UpdateReferences `
                            -ZonesRoot .\game\data\world\zones `
                            -CreateCanonicalFolders

.EXAMPLE
    # JSON + renommage des dossiers zone_N existants
    .\Migrate-CellNaming.ps1 -JsonPath .\lune-noire-data-v19.json `
                            -UpdateReferences `
                            -ZonesRoot .\game\data\world\zones `
                            -ZoneMappingCsv .\zone_mapping.csv `
                            -RenameZones
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$JsonPath,

    [string]$OutputJsonPath,

    [string]$MigrationMapPath,

    [ValidatePattern('^R\d{3}C\d{3}$')]
    [string]$OriginCellId = 'R190C200',

    [switch]$UpdateReferences,

    [string]$ZonesRoot,

    [switch]$CreateCanonicalFolders,

    [string]$ZoneMappingCsv,

    [switch]$RenameZones,

    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# UI
# ---------------------------------------------------------------------------
function Write-Step { param([string]$m) Write-Host ""; Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Info { param([string]$m) Write-Host "    $m" -ForegroundColor Gray }
function Write-Ok   { param([string]$m) Write-Host "    OK  $m" -ForegroundColor Green }
function Write-Skip { param([string]$m) Write-Host "    --  $m" -ForegroundColor DarkYellow }

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function ConvertTo-CardinalIds {
    <#
    .SYNOPSIS
        Renvoie un objet { compact_id; folder_name } pour une cellule R###C###
        relative à une origine donnée.
        - compact_id  : 'N000_E000'        (identifiant cardinal compact, extensible)
        - folder_name : 'cell_N000_E000'   (nom de dossier humain-lisible)
    #>
    param(
        [Parameter(Mandatory)] [string]$CellId,
        [Parameter(Mandatory)] [int]$OriginRow,
        [Parameter(Mandatory)] [int]$OriginCol
    )

    if ($CellId -notmatch '^R(\d{3})C(\d{3})$') {
        throw "ID de cellule invalide : '$CellId' (attendu : R###C###)"
    }

    $row = [int]$Matches[1]
    $col = [int]$Matches[2]

    $nsRaw = $OriginRow - $row    # >0 = nord, <0 = sud
    $ewRaw = $col - $OriginCol    # >0 = est,  <0 = ouest

    if (($nsRaw % 10) -ne 0 -or ($ewRaw % 10) -ne 0) {
        Write-Warning "Cellule $CellId non alignée sur le pas de 10 par rapport à l'origine."
    }

    $ns = [int]([Math]::Round($nsRaw / 10.0))
    $ew = [int]([Math]::Round($ewRaw / 10.0))

    $nsDir = if ($ns -ge 0) { 'N' } else { 'S' }
    $ewDir = if ($ew -ge 0) { 'E' } else { 'W' }

    $compact = '{0}{1:D3}_{2}{3:D3}' -f $nsDir, [Math]::Abs($ns), $ewDir, [Math]::Abs($ew)
    $folder  = 'cell_' + $compact

    return [PSCustomObject]@{
        compact_id  = $compact
        folder_name = $folder
    }
}

function Set-OrUpdate-NoteProperty {
    param([Parameter(Mandatory)] $Object,
          [Parameter(Mandatory)] [string]$Name,
          [Parameter(Mandatory)] $Value)
    if ($Object.PSObject.Properties.Name -contains $Name) {
        $Object.$Name = $Value
    } else {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

function Write-Utf8NoBom {
    param([Parameter(Mandatory)] [string]$Path,
          [Parameter(Mandatory)] [string]$Content)
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $enc)
}

function ConvertFrom-EscapedUnicode {
    <#
    .SYNOPSIS
        Décode toutes les séquences \uXXXX d'une chaîne JSON en leurs caractères réels.

    .DESCRIPTION
        ConvertTo-Json (Windows PowerShell 5.1 et PowerShell 7) échappe par défaut
        tous les caractères non-ASCII (apostrophes, accents, ...) en \uXXXX.
        Cette fonction restaure les caractères Unicode tels qu'ils apparaissent
        dans un JSON "lisible" (ex : "L\u0027hynn" -> "L'hynn", "Aérolithe").

        Les séquences déjà échappées par le standard JSON (\" \\ \/ \b \f \n \r \t)
        sont conservées intactes : on ne touche qu'aux \uXXXX.
    #>
    param(
        [Parameter(Mandatory)] [string]$JsonText
    )

    $regex = [regex]'\\u([0-9a-fA-F]{4})'
    $evaluator = {
        param($m)
        $code = [Convert]::ToInt32($m.Groups[1].Value, 16)
        # On ne décode pas les caractères de contrôle ASCII (<0x20), qui doivent
        # rester échappés en JSON valide. Pour les autres, on rend le caractère réel.
        if ($code -lt 0x20) {
            return $m.Value
        }
        return [char]$code
    }
    return $regex.Replace($JsonText, $evaluator)
}

function Update-LegacyReferencesInPlace {
    <#
    .SYNOPSIS
        Parcourt récursivement un objet et remplace toute valeur string qui
        correspond exactement à un id legacy connu (R###C###) par sa version compacte.
        Compte le nombre de remplacements effectués.
    #>
    param(
        [Parameter(Mandatory)] $Node,
        [Parameter(Mandatory)] [hashtable]$LegacyToCompact,
        [Parameter(Mandatory)] [ref]$Counter
    )

    if ($null -eq $Node) { return }

    if ($Node -is [System.Collections.IList]) {
        for ($i = 0; $i -lt $Node.Count; $i++) {
            $item = $Node[$i]
            if ($item -is [string]) {
                if ($LegacyToCompact.ContainsKey($item)) {
                    $Node[$i] = $LegacyToCompact[$item]
                    $Counter.Value++
                }
            } elseif ($item -is [psobject] -or $item -is [System.Collections.IList]) {
                Update-LegacyReferencesInPlace -Node $item -LegacyToCompact $LegacyToCompact -Counter $Counter
            }
        }
        return
    }

    if ($Node -is [psobject]) {
        foreach ($prop in @($Node.PSObject.Properties)) {
            $val = $prop.Value
            if ($val -is [string]) {
                if ($LegacyToCompact.ContainsKey($val)) {
                    $prop.Value = $LegacyToCompact[$val]
                    $Counter.Value++
                }
            } elseif ($val -is [psobject] -or $val -is [System.Collections.IList]) {
                Update-LegacyReferencesInPlace -Node $val -LegacyToCompact $LegacyToCompact -Counter $Counter
            }
        }
    }
}

# ---------------------------------------------------------------------------
# 1. Lecture du JSON
# ---------------------------------------------------------------------------
Write-Step "Lecture de $JsonPath"

if (-not (Test-Path -LiteralPath $JsonPath)) {
    throw "Fichier introuvable : $JsonPath"
}

$raw  = Get-Content -LiteralPath $JsonPath -Raw -Encoding UTF8
$data = $raw | ConvertFrom-Json

if (-not $data.cells) {
    throw "JSON invalide : champ 'cells' absent."
}
if ($data.cells -isnot [System.Collections.IList]) {
    throw "JSON invalide : 'cells' doit être un tableau (trouvé : $($data.cells.GetType().Name))."
}

# Origine
if ($OriginCellId -notmatch '^R(\d{3})C(\d{3})$') {
    throw "OriginCellId invalide : $OriginCellId"
}
$originRow = [int]$Matches[1]
$originCol = [int]$Matches[2]

$cellCount = $data.cells.Count
Write-Ok ("Cellules détectées : {0}" -f $cellCount)
Write-Ok ("Origine            : {0} (Oracle)" -f $OriginCellId)

# ---------------------------------------------------------------------------
# 2. Calcul des identifiants cardinaux
# ---------------------------------------------------------------------------
Write-Step "Calcul des identifiants cardinaux"

$legacyToCompact = @{}   # R###C### -> N000_E000
$legacyToFolder  = @{}   # R###C### -> cell_N000_E000
$compactSeen     = @{}   # détection de collisions

foreach ($cell in $data.cells) {
    $legacy = $cell.id
    if (-not $legacy) { throw "Cellule sans champ 'id' rencontrée." }
    if ($legacy -notmatch '^R\d{3}C\d{3}$') {
        Write-Warning "  Ignorée (id non-legacy) : $legacy"
        continue
    }

    $ids = ConvertTo-CardinalIds -CellId $legacy -OriginRow $originRow -OriginCol $originCol

    if ($compactSeen.ContainsKey($ids.compact_id)) {
        Write-Warning "  Collision sur '$($ids.compact_id)' (legacy : $legacy vs $($compactSeen[$ids.compact_id]))"
    } else {
        $compactSeen[$ids.compact_id] = $legacy
    }

    $legacyToCompact[$legacy] = $ids.compact_id
    $legacyToFolder[$legacy]  = $ids.folder_name
}

Write-Ok ("{0} identifiants cardinaux générés" -f $legacyToCompact.Count)

# Échantillon
Write-Info "Exemples :"
$sampleKeys = @($OriginCellId)
$sampleKeys += @($legacyToCompact.Keys | Where-Object { $_ -ne $OriginCellId } | Select-Object -First 4)
foreach ($k in $sampleKeys) {
    if ($legacyToCompact.ContainsKey($k)) {
        Write-Info ("  {0,-10}  ->  id={1,-12}  name={2}" -f $k, $legacyToCompact[$k], $legacyToFolder[$k])
    }
}

# ---------------------------------------------------------------------------
# 3. Mise à jour des cellules (préservation de tous les sous-champs)
# ---------------------------------------------------------------------------
Write-Step "Mise à jour des entrées du tableau 'cells'"

$updated = 0
foreach ($cell in $data.cells) {
    $legacy = $cell.id
    if (-not $legacyToCompact.ContainsKey($legacy)) { continue }

    $newId   = $legacyToCompact[$legacy]
    $newName = $legacyToFolder[$legacy]

    # legacy_id préservé d'abord, AVANT d'écraser id
    Set-OrUpdate-NoteProperty -Object $cell -Name 'legacy_id' -Value $legacy

    # Nouveaux id / name
    $cell.id   = $newId
    $cell.name = $newName

    $updated++
}

Write-Ok ("{0} cellules mises à jour (id, name, legacy_id)" -f $updated)
Write-Info "Tous les autres champs (region_id, terrain_type, has_lake, has_river, connects_to, etc.) sont préservés."

# ---------------------------------------------------------------------------
# 4. Mise à jour des références ailleurs dans le JSON
# ---------------------------------------------------------------------------
if ($UpdateReferences) {
    Write-Step "Mise à jour des références R###C### dans le reste du JSON"

    # On exclut le tableau 'cells' (déjà traité) en le détachant temporairement
    $savedCells = $data.cells
    $data.PSObject.Properties.Remove('cells')

    $refCounter = [ref]0
    Update-LegacyReferencesInPlace -Node $data -LegacyToCompact $legacyToCompact -Counter $refCounter

    # Réattachement
    $data | Add-Member -NotePropertyName 'cells' -NotePropertyValue $savedCells

    Write-Ok ("{0} référence(s) mises à jour (ex : villes[*].cell, etc.)" -f $refCounter.Value)
} else {
    Write-Skip "Références hors 'cells' non touchées (utiliser -UpdateReferences pour les migrer)."
}

# ---------------------------------------------------------------------------
# 5. meta.origin
# ---------------------------------------------------------------------------
Write-Step "Enrichissement du bloc meta"

if (-not $data.meta) {
    $data | Add-Member -NotePropertyName 'meta' -NotePropertyValue ([PSCustomObject]@{})
}

$originBlock = [PSCustomObject]@{
    legacy_id      = $OriginCellId
    id             = $legacyToCompact[$OriginCellId]
    name           = $legacyToFolder[$OriginCellId]
    lore_anchor    = 'Oracle'
    lore_reference = "Ouvrage de référence -- centre canonique du monde"
    frozen         = $true
    frozen_date    = (Get-Date -Format 'yyyy-MM-dd')
}

Set-OrUpdate-NoteProperty -Object $data.meta -Name 'origin' -Value $originBlock
Write-Ok ("meta.origin défini sur {0} -> {1}" -f $OriginCellId, $legacyToCompact[$OriginCellId])

# ---------------------------------------------------------------------------
# 6. Écriture des fichiers
# ---------------------------------------------------------------------------
Write-Step "Écriture des fichiers"

if (-not $OutputJsonPath)   { $OutputJsonPath   = $JsonPath }
if (-not $MigrationMapPath) {
    $MigrationMapPath = Join-Path (Split-Path -Parent ([System.IO.Path]::GetFullPath($JsonPath))) 'MIGRATION_MAP.json'
}

$jsonOut = $data | ConvertTo-Json -Depth 100

# Index lookup : legacy_id (R###C###) -> cellule complète (déjà mise à jour à ce stade :
# elle contient id=N###_E###, name=cell_N###_E###, legacy_id=R###C###, + TOUS les sous-champs).
$cellByLegacy = @{}
foreach ($c in $data.cells) {
    if ($c.PSObject.Properties.Name -contains 'legacy_id' -and $c.legacy_id) {
        $cellByLegacy[[string]$c.legacy_id] = $c
    }
}

# Construction du mapping :
#   - clé   = valeur du champ 'name' (ex : 'cell_N018_W018')
#   - valeur = clone complet de la cellule (tous ses sous-champs)
# Tri par nom canonique pour une lecture stable.
$mapMappingObj  = [PSCustomObject]@{}
$legacyToName   = [PSCustomObject]@{}
$sortedLegacy   = @($legacyToFolder.Keys | Sort-Object { $legacyToFolder[$_] })

foreach ($legacy in $sortedLegacy) {
    $folderName = $legacyToFolder[$legacy]
    $cell       = $cellByLegacy[$legacy]

    if ($cell) {
        # Clone profond (déconnecte du JSON principal pour éviter aliasing à la sérialisation)
        $clone = $cell | ConvertTo-Json -Depth 100 -Compress | ConvertFrom-Json
        $mapMappingObj | Add-Member -NotePropertyName $folderName -NotePropertyValue $clone
    } else {
        # Fallback (ne devrait pas survenir)
        $mapMappingObj | Add-Member -NotePropertyName $folderName -NotePropertyValue ([PSCustomObject]@{
            id        = $legacyToCompact[$legacy]
            name      = $folderName
            legacy_id = $legacy
        })
    }

    # Table inverse legacy_id -> name (utilitaire pour le code legacy)
    $legacyToName | Add-Member -NotePropertyName $legacy -NotePropertyValue $folderName
}

$mapOutObj = [PSCustomObject]@{
    generated_at    = (Get-Date -Format 'o')
    origin_legacy   = $OriginCellId
    origin_id       = $legacyToCompact[$OriginCellId]
    origin_name     = $legacyToFolder[$OriginCellId]
    cell_count      = $legacyToCompact.Count
    note            = "Lookup table indexée par 'name' (cell_N###_E###). Chaque valeur contient la cellule complète avec tous ses sous-champs (id, name, legacy_id, region_id, terrain_type, has_lake, lake_count, has_river, river_count, has_mountain_range, river_direction, connects_to, creature_ids, visual_elements, notes, coord_x, coord_y, ...)."
    legacy_to_name  = $legacyToName
    mapping         = $mapMappingObj
}
$mapOut = $mapOutObj | ConvertTo-Json -Depth 10

# Décodage des séquences \uXXXX produites par ConvertTo-Json :
# restitue les caractères Unicode (apostrophes, accents) tels qu'ils
# apparaissent dans le JSON de référence (ex : "L'hynn" au lieu de "L\u0027hynn").
$jsonOut = ConvertFrom-EscapedUnicode -JsonText $jsonOut
$mapOut  = ConvertFrom-EscapedUnicode -JsonText $mapOut

if ($DryRun) {
    Write-Skip "(DryRun) Écriture ignorée : $OutputJsonPath"
    Write-Skip "(DryRun) Écriture ignorée : $MigrationMapPath"
} else {
    if ($OutputJsonPath -eq $JsonPath) {
        $backup = "$JsonPath.bak"
        Copy-Item -LiteralPath $JsonPath -Destination $backup -Force
        Write-Ok "Backup : $backup"
    }
    Write-Utf8NoBom -Path $OutputJsonPath   -Content $jsonOut
    Write-Utf8NoBom -Path $MigrationMapPath -Content $mapOut
    Write-Ok "JSON enrichi      : $OutputJsonPath"
    Write-Ok "Migration map     : $MigrationMapPath"
}

# ---------------------------------------------------------------------------
# 7. Opérations sur les dossiers (optionnelles)
# ---------------------------------------------------------------------------
if ($CreateCanonicalFolders) {
    Write-Step "Création des dossiers canoniques sous $ZonesRoot"

    if (-not $ZonesRoot) { throw "-CreateCanonicalFolders requiert -ZonesRoot." }
    if (-not (Test-Path -LiteralPath $ZonesRoot)) {
        if ($DryRun) {
            Write-Skip "(DryRun) Création de $ZonesRoot"
        } else {
            New-Item -ItemType Directory -Path $ZonesRoot -Force | Out-Null
            Write-Ok "Création de $ZonesRoot"
        }
    }

    $created = 0; $existed = 0
    foreach ($k in $legacyToFolder.Keys) {
        $target = Join-Path $ZonesRoot $legacyToFolder[$k]
        if (Test-Path -LiteralPath $target) {
            $existed++
        } else {
            if (-not $DryRun) {
                New-Item -ItemType Directory -Path $target -Force | Out-Null
            }
            $created++
        }
    }
    if ($DryRun) {
        Write-Skip ("(DryRun) {0} dossier(s) seraient créés, {1} déjà présent(s)" -f $created, $existed)
    } else {
        Write-Ok ("{0} dossier(s) créé(s), {1} déjà présent(s)" -f $created, $existed)
    }
}

if ($RenameZones) {
    Write-Step "Renommage zone_N/ -> cell_N###_E###/"

    if (-not $ZonesRoot)      { throw "-RenameZones requiert -ZonesRoot." }
    if (-not $ZoneMappingCsv) { throw "-RenameZones requiert -ZoneMappingCsv (colonnes : zone_name,cell_legacy_id)." }
    if (-not (Test-Path -LiteralPath $ZonesRoot))      { throw "Répertoire introuvable : $ZonesRoot" }
    if (-not (Test-Path -LiteralPath $ZoneMappingCsv)) { throw "CSV introuvable : $ZoneMappingCsv" }

    $rows = Import-Csv -LiteralPath $ZoneMappingCsv
    if ($rows.Count -gt 0) {
        foreach ($req in @('zone_name', 'cell_legacy_id')) {
            if (-not ($rows[0].PSObject.Properties.Name -contains $req)) {
                throw "Le CSV doit contenir les colonnes : zone_name, cell_legacy_id"
            }
        }
    }

    $renamed = 0; $skipped = 0; $errors = 0
    foreach ($row in $rows) {
        $zoneName = $row.zone_name
        $legacy   = $row.cell_legacy_id

        if (-not $legacyToFolder.ContainsKey($legacy)) {
            Write-Warning "  $zoneName -> $legacy : cellule absente du JSON, ignorée."
            $skipped++; continue
        }

        $newName = $legacyToFolder[$legacy]
        $src = Join-Path $ZonesRoot $zoneName
        $dst = Join-Path $ZonesRoot $newName

        if (-not (Test-Path -LiteralPath $src)) {
            Write-Warning "  $zoneName : source introuvable, ignoré."
            $skipped++; continue
        }
        if (Test-Path -LiteralPath $dst) {
            Write-Warning "  $newName : cible existante, $zoneName non renommé."
            $errors++; continue
        }

        if ($DryRun) {
            Write-Skip ("(DryRun) {0} -> {1}" -f $zoneName, $newName)
        } else {
            Rename-Item -LiteralPath $src -NewName $newName
            Write-Ok ("{0} -> {1}" -f $zoneName, $newName)
        }
        $renamed++
    }

    Write-Host ""
    Write-Ok ("Renommages : {0} | ignorés : {1} | erreurs : {2}" -f $renamed, $skipped, $errors)
}

# ---------------------------------------------------------------------------
# Récap
# ---------------------------------------------------------------------------
Write-Step "Terminé"
Write-Info ("Cellules traitées : {0}" -f $cellCount)
Write-Info ("Origine           : {0}  ->  id={1}  name={2}" -f $OriginCellId, $legacyToCompact[$OriginCellId], $legacyToFolder[$OriginCellId])
if ($DryRun) {
    Write-Info "Mode DryRun : aucune modification écrite sur disque."
}