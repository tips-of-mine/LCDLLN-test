<#
.SYNOPSIS
    Migre la nomenclature des cellules de la carte LCDLLN depuis zone_N/ vers cell_N###_E###/.

.DESCRIPTION
    Ce script :
      1. Lit carte_export.json
      2. Calcule pour chaque cellule R###C### son identifiant cardinal cell_N###_E###
         en utilisant R190C200 (Oracle) comme origine immuable.
      3. Écrit le JSON enrichi avec :
           - meta.origin (référence canonique)
           - tiles.cells[*].cell_dir (nouvel identifiant)
      4. Génère MIGRATION_MAP.json : table R###C### -> cell_dir
      5. (Optionnel) Crée les dossiers canoniques cell_N###_E###/ sous -ZonesRoot
      6. (Optionnel) Renomme les dossiers zone_N/ existants à partir d'un CSV
         de mapping fourni par l'utilisateur (colonnes : zone_name,cell_id)

    L'Oracle (R190C200) est l'origine cosmologique et géographique de référence.
    Toutes les coordonnées cardinales en découlent.

.PARAMETER JsonPath
    Chemin vers carte_export.json (obligatoire).

.PARAMETER OutputJsonPath
    Chemin de sortie pour le JSON enrichi. Par défaut : <JsonPath> écrasé
    (un backup .bak est créé automatiquement).

.PARAMETER MigrationMapPath
    Chemin de sortie pour MIGRATION_MAP.json.
    Par défaut : MIGRATION_MAP.json à côté du JSON source.

.PARAMETER OriginCellId
    ID de la cellule origine au format R###C###. Par défaut : R190C200 (Oracle).

.PARAMETER ZonesRoot
    Racine contenant les dossiers de cellules (zone_N/ actuels, futurs cell_*/).

.PARAMETER ZoneMappingCsv
    CSV avec colonnes "zone_name,cell_id" pour effectuer le renommage
    des dossiers zone_N -> cell_N###_E###. Requis avec -RenameZones.

.PARAMETER CreateCanonicalFolders
    Crée un dossier vide cell_N###_E###/ sous -ZonesRoot pour chaque cellule du JSON.
    Utile pour préparer la structure cible avant d'y déplacer manuellement le contenu.

.PARAMETER RenameZones
    Renomme zone_N/ -> cell_N###_E###/ d'après -ZoneMappingCsv.

.PARAMETER DryRun
    N'écrit/ne renomme rien ; affiche uniquement ce qui serait fait.

.EXAMPLE
    # Enrichissement du JSON uniquement (cas de base)
    .\Migrate-CellNaming.ps1 -JsonPath .\carte_export.json

.EXAMPLE
    # Aperçu sans rien modifier
    .\Migrate-CellNaming.ps1 -JsonPath .\carte_export.json -DryRun

.EXAMPLE
    # Enrichit le JSON + crée les dossiers canoniques vides
    .\Migrate-CellNaming.ps1 -JsonPath .\carte_export.json `
                             -ZonesRoot .\game\data\world\zones `
                             -CreateCanonicalFolders

.EXAMPLE
    # Renomme les dossiers zone_N en cell_N###_E### à partir d'un mapping
    .\Migrate-CellNaming.ps1 -JsonPath .\carte_export.json `
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
    [string]$ZonesRoot,
    [string]$ZoneMappingCsv,
    [switch]$CreateCanonicalFolders,
    [switch]$RenameZones,
    [switch]$DryRun
)

# ---------------------------------------------------------------------------
# Préparation
# ---------------------------------------------------------------------------
$ErrorActionPreference = 'Stop'

Function Write-Step {
    Param([string]$Message)
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

Function Write-Info {
    Param([string]$Message)
    Write-Host "    $Message" -ForegroundColor Gray
}

Function Write-Ok {
    Param([string]$Message)
        Write-Host "    OK  $Message" -ForegroundColor Green
}

Function Write-Skip {
    Param([string]$Message)
    Write-Host "    --  $Message" -ForegroundColor DarkYellow
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

Function ConvertTo-CellDir {
    <#
    .SYNOPSIS
        Convertit un ID de cellule R###C### en identifiant cardinal cell_N###_E###
        relatif à une origine donnée (par défaut R190C200 = Oracle).
    #>
    Param(
        [Parameter(Mandatory)] [string]$CellId,
        [Parameter(Mandatory)] [int]$OriginRow,
        [Parameter(Mandatory)] [int]$OriginCol
    )

    If ($CellId -notmatch '^R(\d{3})C(\d{3})$') {
        throw "ID de cellule invalide : '$CellId' (attendu : R###C###)"
    }

    $row = [int]$Matches[1]
    $col = [int]$Matches[2]

    $nsRaw = $OriginRow - $row    # >0 = nord, <0 = sud
    $ewRaw = $col - $OriginCol    # >0 = est,  <0 = ouest

    if (($nsRaw % 10) -ne 0 -or ($ewRaw % 10) -ne 0) {
        Write-Warning "Cellule $CellId non alignée sur la grille au pas de 10 (par rapport à l'origine)."
    }

    $ns = [int]([Math]::Round($nsRaw / 10.0))
    $ew = [int]([Math]::Round($ewRaw / 10.0))

    $nsDir = if ($ns -ge 0) { 'N' } else { 'S' }
    $ewDir = if ($ew -ge 0) { 'E' } else { 'W' }

    return ('cell_{0}{1:D3}_{2}{3:D3}' -f $nsDir, [Math]::Abs($ns), $ewDir, [Math]::Abs($ew))
}

Function Set-OrUpdate-NoteProperty {
    <#
    .SYNOPSIS
        Ajoute ou met à jour une propriété sur un PSCustomObject.
    #>
    param(
        [Parameter(Mandatory)] $Object,
        [Parameter(Mandatory)] [string]$Name,
        [Parameter(Mandatory)] $Value
    )

    if ($Object.PSObject.Properties.Name -contains $Name) {
        $Object.$Name = $Value
    } else {
        $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
    }
}

Function Write-Utf8NoBom {
    <#
    .SYNOPSIS
        Écrit un fichier texte en UTF-8 sans BOM (PS 5.1 et 7+ compatibles).
    #>
    param(
        [Parameter(Mandatory)] [string]$Path,
        [Parameter(Mandatory)] [string]$Content
    )
    $enc = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $Content, $enc)
}

# ---------------------------------------------------------------------------
# 1. Lecture & validation du JSON
# ---------------------------------------------------------------------------
Write-Step "Lecture de $JsonPath"

If (-not (Test-Path -LiteralPath $JsonPath)) {
    Throw "Fichier introuvable : $JsonPath"
}

$Raw  = Get-Content -LiteralPath $JsonPath -Raw -Encoding UTF8
$Data = $Raw | ConvertFrom-Json

If (-not $Data.tiles -or -not $Data.tiles.cells) {
    Throw "JSON invalide : 'tiles.cells' manquant."
}

# Origine
If ($OriginCellId -notmatch '^R(\d{3})C(\d{3})$') {
    Throw "OriginCellId invalide : $OriginCellId"
}

$OriginRow = [int]$Matches[1]
$OriginCol = [int]$Matches[2]

$Cells     = $Data.tiles.cells
$CellIds   = @($Cells.PSObject.Properties.Name)
$CellCount = $CellIds.Count

If ($CellIds -notcontains $OriginCellId) {
    Write-Warning "L'origine $OriginCellId n'apparaît pas dans tiles.cells (vérifiez la cohérence du JSON)."
}

Write-Ok ("Cellules détectées : {0}" -f $cellCount)
Write-Ok ("Origine            : {0} (Oracle)" -f $OriginCellId)

# ---------------------------------------------------------------------------
# 2. Calcul des cell_dir + construction du mapping
# ---------------------------------------------------------------------------
Write-Step "Calcul des identifiants cardinaux (cell_dir)"

$MigrationMap = [ordered]@{}
$Duplicates   = @{}

Foreach ($CellId in $CellIds) {
    $CellDir = ConvertTo-CellDir -CellId $CellId -OriginRow $OriginRow -OriginCol $OriginCol

    If ($MigrationMap.Values -contains $CellDir) {
        $Duplicates[$cellDir] = $True
    }
    $MigrationMap[$CellId] = $CellDir
}

If ($Duplicates.Count -gt 0) {
    Write-Warning ("Collisions détectées sur {0} identifiant(s) cardinal(aux). Vérifiez OriginCellId et l'alignement de la grille." -f $Duplicates.Count)
}

Write-Ok ("{0} identifiants cardinaux générés" -f $MigrationMap.Count)

# Exemples lisibles
Write-Info "Exemples :"
$Samples = @($OriginCellId) + (@($CellIds | Where-Object { $_ -ne $OriginCellId } | Select-Object -First 4))

Foreach ($S in $Samples) {
    If ($MigrationMap.Contains($S)) {
        Write-Info ("  {0,-10} -> {1}" -f $S, $MigrationMap[$s])
    }
}

# ---------------------------------------------------------------------------
# 3. Enrichissement du JSON en mémoire
# ---------------------------------------------------------------------------
Write-Step "Enrichissement du JSON (meta.origin + tiles.Cells[*].Cell_dir)"

# Bloc meta.origin
$OriginBlock = [PSCustomObject]@{
    cell_id        = $OriginCellId
    cell_dir       = $MigrationMap[$OriginCellId]
    lore_anchor    = 'Oracle'
    lore_reference = "Ouvrage de référence — centre canonique du monde"
    frozen         = $True
    frozen_date    = (Get-Date -Format 'yyyy-MM-dd')
}

If (-not $Data.meta) {
    $Data | Add-Member -NotePropertyName 'meta' -NotePropertyValue ([PSCustomObject]@{})
}
Set-OrUpdate-NoteProperty -Object $Data.meta -Name 'origin' -Value $OriginBlock

# cell_dir sur chaque cellule
$Enriched = 0
Foreach ($CellId in $CellIds) {
    $Cell = $Cells.$CellId
    Set-OrUpdate-NoteProperty -Object $cell -Name 'cell_dir' -Value $MigrationMap[$CellId]
    $Enriched++
}

Write-Ok ("{0} cellules enrichies (cell_dir ajouté/mis à jour)" -f $Enriched)

# ---------------------------------------------------------------------------
# 4. Écriture du JSON enrichi + MIGRATION_MAP.json
# ---------------------------------------------------------------------------
Write-Step "Écriture des fichiers"

If (-not $OutputJsonPath) { $OutputJsonPath = $JsonPath }

If (-not $MigrationMapPath) {
    $MigrationMapPath = Join-Path (Split-Path -Parent ([System.IO.Path]::GetFullPath($JsonPath))) 'MIGRATION_MAP.json'
}

$JsonOut = $Data | ConvertTo-Json -Depth 100

$MapOutObj = [PSCustomObject]@{
    generated_at    = (Get-Date -Format 'o')
    origin_cell_id  = $OriginCellId
    origin_cell_dir = $MigrationMap[$OriginCellId]
    note            = "Mapping R###C### -> cell_dir (origine = Oracle, R190C200). Sert de référence pour le code legacy."
    mapping         = $MigrationMap
}
$MapOut = $MapOutObj | ConvertTo-Json -Depth 10

If ($DryRun) {
    Write-Skip "(DryRun) Écriture ignorée : $OutputJsonPath"
    Write-Skip "(DryRun) Écriture ignorée : $MigrationMapPath"
}
Else {
    # Backup du JSON original si on écrase
    If ($OutputJsonPath -eq $JsonPath) {
        $Backup = "$JsonPath.bak"
        Copy-Item -LiteralPath $JsonPath -Destination $Backup -Force
        Write-Ok "Backup : $Backup"
    }
    Write-Utf8NoBom -Path $OutputJsonPath    -Content $jsonOut
    Write-Utf8NoBom -Path $MigrationMapPath  -Content $mapOut
    Write-Ok "JSON enrichi      : $OutputJsonPath"
    Write-Ok "Migration map     : $MigrationMapPath"
}

# ---------------------------------------------------------------------------
# 5. Opérations sur les dossiers (optionnelles)
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
    Foreach ($cellId in $cellIds) {
        $target = Join-Path $ZonesRoot $migrationMap[$cellId]
        If (Test-Path -LiteralPath $target) {
            $existed++
        }
        Else {
            If ($DryRun) {
                # Compté comme "à créer" mais pas créé
                $created++
            }
            Else {
                New-Item -ItemType Directory -Path $target -Force | Out-Null
                $created++
            }
        }
    }

    If ($DryRun) {
        Write-Skip ("(DryRun) {0} dossier(s) seraient créés, {1} existent déjà" -f $created, $existed)
    }
    Else {
        Write-Ok ("{0} dossier(s) créé(s), {1} déjà présent(s)" -f $created, $existed)
    }
}

If ($RenameZones) {
    Write-Step "Renommage zone_N/ -> cell_N###_E###/"

    If (-not $ZonesRoot)      { Throw "-RenameZones requiert -ZonesRoot." }
    If (-not $ZoneMappingCsv) { Throw "-RenameZones requiert -ZoneMappingCsv (colonnes : zone_name,cell_id)." }
    If (-not (Test-Path -LiteralPath $ZonesRoot))      { Throw "Répertoire introuvable : $ZonesRoot" }
    If (-not (Test-Path -LiteralPath $ZoneMappingCsv)) { Throw "Fichier CSV introuvable : $ZoneMappingCsv" }

    $mappingRows = Import-Csv -LiteralPath $ZoneMappingCsv
    $requiredCols = @('zone_name', 'cell_id')
    Foreach ($col in $requiredCols) {
        If ($mappingRows.Count -gt 0 -and -not ($mappingRows[0].PSObject.Properties.Name -contains $col)) {
            Throw "Le CSV doit contenir les colonnes : $($requiredCols -join ', ')"
        }
    }

    $renamed = 0; $skipped = 0; $errors = 0
    Foreach ($row in $mappingRows) {
        $zoneName = $Row.zone_name
        $cellId   = $Row.cell_id

        If (-not $migrationMap.Contains($cellId)) {
            Write-Warning "  $zoneName -> $cellId : cellule absente du JSON, ignorée."
            $skipped++; continue
        }

        $newName  = $migrationMap[$cellId]
        $src = Join-Path $ZonesRoot $zoneName
        $dst = Join-Path $ZonesRoot $newName

        If (-not (Test-Path -LiteralPath $src)) {
            Write-Warning "  $zoneName : dossier source introuvable, ignoré."
            $skipped++; continue
        }
        If (Test-Path -LiteralPath $dst) {
            Write-Warning "  $newName : déjà existant, $zoneName non renommé."
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
    Write-Ok ("Renommages traités : {0} | ignorés : {1} | erreurs : {2}" -f $renamed, $skipped, $errors)
}

# ---------------------------------------------------------------------------
# Récapitulatif
# ---------------------------------------------------------------------------
Write-Step "Terminé"
Write-Info ("Cellules traitées : {0}" -f $cellCount)
Write-Info ("Origine           : {0} -> {1}" -f $OriginCellId, $migrationMap[$OriginCellId])

If ($DryRun) {
    Write-Info "Mode DryRun : aucune modification écrite sur disque."
}
