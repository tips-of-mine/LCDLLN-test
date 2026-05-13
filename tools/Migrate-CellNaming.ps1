<#
.SYNOPSIS
    Migre la nomenclature des cellules du JSON lune-noire-data vers un système
    de coordonnées cardinales extensible, ancré sur l'Oracle (R190C200).

.DESCRIPTION
    Pour chaque entrée du tableau `cells` du JSON :
      - legacy_id  = ancien R###C###                     (préservé pour rétrocompat)
      - id         = N###_E### / N###_W### / S###_E### …  (identIfiant cardinal compact,
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

.ParamETER JsonPath
    Chemin vers le JSON source (ex : lune-noire-data-v19.json).

.ParamETER OutputJsonPath
    Chemin de sortie. Par défaut : écrase le source (un .bak est créé).

.ParamETER MigrationMapPath
    Chemin de sortie pour MIGRATION_MAP.json. Par défaut : à côté du JSON source.

.ParamETER OriginCellId
    Cellule origine au format R###C###. Par défaut : R190C200 (Oracle).

.ParamETER UpdateReferences
    Met également à jour toutes les chaînes de caractères R###C### trouvées
    AILLEURS dans le JSON (ex : villes[*].cell) vers le nouveau id cardinal.

.ParamETER ZonesRoot
    Racine des dossiers de cellules (zone_N/ actuels, futurs cell_*).

.ParamETER CreateCanonicalFolders
    Crée un dossier vide cell_N###_E###/ pour chaque cellule du JSON.

.ParamETER ZoneMappingCsv
    CSV (colonnes : zone_name,cell_legacy_id) pour renommer zone_N/ -> cell_N###_E###/.

.ParamETER RenameZones
    Effectue le renommage d'après -ZoneMappingCsv.

.ParamETER DryRun
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
Param(
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
Function Write-Step { Param([string]$M) Write-Host ""; Write-Host "==> $M" -ForegroundColor Cyan }
Function Write-Info { Param([string]$M) Write-Host "    $M" -ForegroundColor Gray }
Function Write-Ok   { Param([string]$M) Write-Host "    OK  $M" -ForegroundColor Green }
Function Write-Skip { Param([string]$M) Write-Host "    --  $M" -ForegroundColor DarkYellow }

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
Function ConvertTo-CardinalIds {
    <#
    .SYNOPSIS
        Renvoie un objet { compact_id; folder_name } pour une cellule R###C###
        relative à une origine donnée.
        - compact_id  : 'N000_E000'        (identIfiant cardinal compact, extensible)
        - folder_name : 'cell_N000_E000'   (nom de dossier humain-lisible)
    #>
    Param(
        [Parameter(Mandatory)] [string]$CellId,
        [Parameter(Mandatory)] [int]$OriginRow,
        [Parameter(Mandatory)] [int]$OriginCol
    )

    If ($CellId -notmatch '^R(\d{3})C(\d{3})$') {
        Throw "ID de cellule invalide : '$CellId' (attendu : R###C###)"
    }

    $Row = [int]$Matches[1]
    $Col = [int]$Matches[2]

    $nsRaw = $OriginRow - $Row    # >0 = nord, <0 = sud
    $ewRaw = $Col - $OriginCol    # >0 = est,  <0 = ouest

    If (($nsRaw % 10) -ne 0 -or ($ewRaw % 10) -ne 0) {
        Write-Warning "Cellule $CellId non alignée sur le pas de 10 par rapport à l'origine."
    }

    $ns = [int]([Math]::Round($nsRaw / 10.0))
    $ew = [int]([Math]::Round($ewRaw / 10.0))

    $nsDir = If ($ns -ge 0) { 'N' } else { 'S' }
    $ewDir = If ($ew -ge 0) { 'E' } else { 'W' }

    $compact = '{0}{1:D3}_{2}{3:D3}' -f $nsDir, [Math]::Abs($ns), $ewDir, [Math]::Abs($ew)
    $folder  = 'cell_' + $compact

    Return [PSCustomObject]@{
        compact_id  = $compact
        folder_name = $folder
    }
}

Function Set-OrUpdate-NoteProperty {
    Param([Parameter(Mandatory)] $Object,
          [Parameter(Mandatory)] [string]$Name,
          [Parameter(Mandatory)] $Value)
    If ($Object.PSObject.Properties.Name -contains $Name) {
        $Object.$Name = $Value
    }
    Else {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

Function Write-Utf8NoBom {
    Param([Parameter(Mandatory)] [string]$Path,
          [Parameter(Mandatory)] [string]$Content)
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $enc)
}

Function Update-LegacyReferencesInPlace {
    <#
    .SYNOPSIS
        Parcourt récursivement un objet et remplace toute valeur string qui
        correspond exactement à un id legacy connu (R###C###) par sa version compacte.
        Compte le nombre de remplacements effectués.
    #>
    Param(
        [Parameter(Mandatory)] $Node,
        [Parameter(Mandatory)] [hashtable]$LegacyToCompact,
        [Parameter(Mandatory)] [ref]$Counter
    )

    If ($null -eq $Node) { return }

    If ($Node -is [System.Collections.IList]) {
        For ($i = 0; $i -lt $Node.Count; $i++) {
            $item = $Node[$i]
            If ($item -is [string]) {
                If ($LegacyToCompact.ContainsKey($item)) {
                    $Node[$i] = $LegacyToCompact[$item]
                    $Counter.Value++
                }
            }
            ElseIf ($item -is [psobject] -or $item -is [System.Collections.IList]) {
                Update-LegacyReferencesInPlace -Node $item -LegacyToCompact $LegacyToCompact -Counter $Counter
            }
        }
        Return
    }

    If ($Node -is [psobject]) {
        Foreach ($prop in @($Node.PSObject.Properties)) {
            $val = $prop.Value
            If ($val -is [string]) {
                If ($LegacyToCompact.ContainsKey($val)) {
                    $prop.Value = $LegacyToCompact[$val]
                    $Counter.Value++
                }
            }
            ElseIf ($val -is [psobject] -or $val -is [System.Collections.IList]) {
                Update-LegacyReferencesInPlace -Node $val -LegacyToCompact $LegacyToCompact -Counter $Counter
            }
        }
    }
}

# ---------------------------------------------------------------------------
# 1. Lecture du JSON
# ---------------------------------------------------------------------------
Write-Step "Lecture de $JsonPath"

If (-not (Test-Path -LiteralPath $JsonPath)) {
    Throw "Fichier introuvable : $JsonPath"
}

$raw  = Get-Content -LiteralPath $JsonPath -Raw -Encoding UTF8
$data = $raw | ConvertFrom-Json

If (-not $data.cells) {
    Throw "JSON invalide : champ 'cells' absent."
}

If ($data.cells -isnot [System.Collections.IList]) {
    Throw "JSON invalide : 'cells' doit être un tableau (trouvé : $($data.cells.GetType().Name))."
}

# Origine
If ($OriginCellId -notmatch '^R(\d{3})C(\d{3})$') {
    Throw "OriginCellId invalide : $OriginCellId"
}
$originRow = [int]$Matches[1]
$originCol = [int]$Matches[2]

$cellCount = $data.cells.Count
Write-Ok ("Cellules détectées : {0}" -f $cellCount)
Write-Ok ("Origine            : {0} (Oracle)" -f $OriginCellId)

# ---------------------------------------------------------------------------
# 2. Calcul des identIfiants cardinaux
# ---------------------------------------------------------------------------
Write-Step "Calcul des identIfiants cardinaux"

$legacyToCompact = @{}   # R###C### -> N000_E000
$legacyToFolder  = @{}   # R###C### -> cell_N000_E000
$compactSeen     = @{}   # détection de collisions

Foreach ($cell in $data.cells) {
    $legacy = $cell.id
    If (-not $legacy) { Throw "Cellule sans champ 'id' rencontrée." }

    If ($legacy -notmatch '^R\d{3}C\d{3}$') {
        Write-Warning "  Ignorée (id non-legacy) : $legacy"
        Continue
    }

    $ids = ConvertTo-CardinalIds -CellId $legacy -OriginRow $originRow -OriginCol $originCol

    If ($compactSeen.ContainsKey($ids.compact_id)) {
        Write-Warning "  Collision sur '$($ids.compact_id)' (legacy : $legacy vs $($compactSeen[$ids.compact_id]))"
    }
    Else {
        $compactSeen[$ids.compact_id] = $legacy
    }

    $legacyToCompact[$legacy] = $ids.compact_id
    $legacyToFolder[$legacy]  = $ids.folder_name
}

Write-Ok ("{0} identIfiants cardinaux générés" -f $legacyToCompact.Count)

# Échantillon
Write-Info "Exemples :"
$sampleKeys = @($OriginCellId)
$sampleKeys += @($legacyToCompact.Keys | Where-Object { $_ -ne $OriginCellId } | Select-Object -First 4)

Foreach ($k in $sampleKeys) {
    If ($legacyToCompact.ContainsKey($k)) {
        Write-Info ("  {0,-10}  ->  id={1,-12}  name={2}" -f $k, $legacyToCompact[$k], $legacyToFolder[$k])
    }
}

# ---------------------------------------------------------------------------
# 3. Mise à jour des cellules (préservation de tous les sous-champs)
# ---------------------------------------------------------------------------
Write-Step "Mise à jour des entrées du tableau 'cells'"

$updated = 0
Foreach ($cell in $data.cells) {
    $legacy = $cell.id
    If (-not $legacyToCompact.ContainsKey($legacy)) { continue }

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
If ($UpdateReferences) {
    Write-Step "Mise à jour des références R###C### dans le reste du JSON"

    # On exclut le tableau 'cells' (déjà traité) en le détachant temporairement
    $savedCells = $data.cells
    $data.PSObject.Properties.Remove('cells')

    $refCounter = [ref]0
    Update-LegacyReferencesInPlace -Node $data -LegacyToCompact $legacyToCompact -Counter $refCounter

    # Réattachement
    $data | Add-Member -NotePropertyName 'cells' -NotePropertyValue $savedCells

    Write-Ok ("{0} référence(s) mises à jour (ex : villes[*].cell, etc.)" -f $refCounter.Value)
}
Else {
    Write-Skip "Références hors 'cells' non touchées (utiliser -UpdateReferences pour les migrer)."
}

# ---------------------------------------------------------------------------
# 5. meta.origin
# ---------------------------------------------------------------------------
Write-Step "Enrichissement du bloc meta"

If (-not $data.meta) {
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

If (-not $OutputJsonPath)   { $OutputJsonPath   = $JsonPath }

If (-not $MigrationMapPath) {
    $MigrationMapPath = Join-Path (Split-Path -Parent ([System.IO.Path]::GetFullPath($JsonPath))) 'MIGRATION_MAP.json'
}

$jsonOut = $data | ConvertTo-Json -Depth 100

$mapMappingObj = [PSCustomObject]@{}
Foreach ($k in ($legacyToCompact.Keys | Sort-Object)) {
    $entry = [PSCustomObject]@{
        id   = $legacyToCompact[$k]
        name = $legacyToFolder[$k]
    }
    $mapMappingObj | Add-Member -NotePropertyName $k -NotePropertyValue $entry
}

$mapOutObj = [PSCustomObject]@{
    generated_at  = (Get-Date -Format 'o')
    origin_legacy = $OriginCellId
    origin_id     = $legacyToCompact[$OriginCellId]
    origin_name   = $legacyToFolder[$OriginCellId]
    cell_count    = $legacyToCompact.Count
    note          = "Table de migration R###C### -> { id cardinal compact, name humain }. Origine : Oracle."
    mapping       = $mapMappingObj
}
$mapOut = $mapOutObj | ConvertTo-Json -Depth 10

If ($DryRun) {
    Write-Skip "(DryRun) Écriture ignorée : $OutputJsonPath"
    Write-Skip "(DryRun) Écriture ignorée : $MigrationMapPath"
}
Else {
    If ($OutputJsonPath -eq $JsonPath) {
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
If ($CreateCanonicalFolders) {
    Write-Step "Création des dossiers canoniques sous $ZonesRoot"

    If (-not $ZonesRoot) { Throw "-CreateCanonicalFolders requiert -ZonesRoot." }
    If (-not (Test-Path -LiteralPath $ZonesRoot)) {
        If ($DryRun) {
            Write-Skip "(DryRun) Création de $ZonesRoot"
        }
        Else {
            New-Item -ItemType Directory -Path $ZonesRoot -Force | Out-Null
            Write-Ok "Création de $ZonesRoot"
        }
    }

    $created = 0; $existed = 0
    Foreach ($k in $legacyToFolder.Keys) {
        $target = Join-Path $ZonesRoot $legacyToFolder[$k]
        If (Test-Path -LiteralPath $target) {
            $existed++
        }
        Else {
            If (-not $DryRun) {
                New-Item -ItemType Directory -Path $target -Force | Out-Null
            }
            $created++
        }
    }
    If ($DryRun) {
        Write-Skip ("(DryRun) {0} dossier(s) seraient créés, {1} déjà présent(s)" -f $created, $existed)
    }
    Else {
        Write-Ok ("{0} dossier(s) créé(s), {1} déjà présent(s)" -f $created, $existed)
    }
}

If ($RenameZones) {
    Write-Step "Renommage zone_N/ -> cell_N###_E###/"

    If (-not $ZonesRoot)      { Throw "-RenameZones requiert -ZonesRoot." }
    If (-not $ZoneMappingCsv) { Throw "-RenameZones requiert -ZoneMappingCsv (colonnes : zone_name,cell_legacy_id)." }
    If (-not (Test-Path -LiteralPath $ZonesRoot))      { Throw "Répertoire introuvable : $ZonesRoot" }
    If (-not (Test-Path -LiteralPath $ZoneMappingCsv)) { Throw "CSV introuvable : $ZoneMappingCsv" }

    $rows = Import-Csv -LiteralPath $ZoneMappingCsv
    If ($rows.Count -gt 0) {
        Foreach ($req in @('zone_name', 'cell_legacy_id')) {
            If (-not ($rows[0].PSObject.Properties.Name -contains $req)) {
                Throw "Le CSV doit contenir les colonnes : zone_name, cell_legacy_id"
            }
        }
    }

    $renamed = 0; $skipped = 0; $errors = 0
    Foreach ($row in $rows) {
        $zoneName = $row.zone_name
        $legacy   = $row.cell_legacy_id

        If (-not $legacyToFolder.ContainsKey($legacy)) {
            Write-Warning "  $zoneName -> $legacy : cellule absente du JSON, ignorée."
            $skipped++; continue
        }

        $newName = $legacyToFolder[$legacy]
        $src = Join-Path $ZonesRoot $zoneName
        $dst = Join-Path $ZonesRoot $newName

        If (-not (Test-Path -LiteralPath $src)) {
            Write-Warning "  $zoneName : source introuvable, ignoré."
            $skipped++; continue
        }

        If (Test-Path -LiteralPath $dst) {
            Write-Warning "  $newName : cible existante, $zoneName non renommé."
            $errors++; continue
        }

        If ($DryRun) {
            Write-Skip ("(DryRun) {0} -> {1}" -f $zoneName, $newName)
        }
        Else {
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
If ($DryRun) {
    Write-Info "Mode DryRun : aucune modIfication écrite sur disque."
}
