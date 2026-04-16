#Requires -Version 5.1
<#
  Après export runtime depuis le World Editor (zones/<ZONE_ID>/ incluant layout_from_editor.json),
  invoque zone_builder pour générer chunks/ et méta-zone packagée.

  Variables d'environnement :
    ZONE_ID           (obligatoire) identifiant de zone aligné sur l'export WE
    LCDLLN_BUILD_DIR  optionnel, défaut : build/vs2022-x64
    ZONE_BUILDER      optionnel, chemin complet vers zone_builder(.exe)

  Usage (depuis la racine du dépôt) :
    $env:ZONE_ID = "ma_zone"
    .\tools\world\export_zone_with_chunks.ps1
#>
$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $RepoRoot

if (-not $env:ZONE_ID -or [string]::IsNullOrWhiteSpace($env:ZONE_ID)) {
    Write-Error "Définir la variable d'environnement ZONE_ID (ex. ma_zone)."
}

$ZoneId = $env:ZONE_ID.Trim()
$BuildRel = if ($env:LCDLLN_BUILD_DIR) { $env:LCDLLN_BUILD_DIR.Trim() } else { "build/vs2022-x64" }

$Zb = $null
if ($env:ZONE_BUILDER -and (Test-Path -LiteralPath $env:ZONE_BUILDER)) {
    $Zb = $env:ZONE_BUILDER
}
else {
    $Candidate = Join-Path $RepoRoot (Join-Path $BuildRel "pkg\zone_builder\zone_builder.exe")
    if (Test-Path -LiteralPath $Candidate) {
        $Zb = $Candidate
    }
}

if (-not $Zb) {
    Write-Error "Exécutable zone_builder introuvable. Compiler le projet ou définir ZONE_BUILDER (chemin vers zone_builder.exe). Cherché : $BuildRel\pkg\zone_builder\zone_builder.exe"
}

$Config = Join-Path $RepoRoot "config.json"
if (-not (Test-Path -LiteralPath $Config)) {
    Write-Error "config.json introuvable à la racine du dépôt : $Config"
}

$LayoutRel = "zones/$ZoneId/layout_from_editor.json"
Write-Host "[export_zone_with_chunks] zone_builder : $Zb"
Write-Host "[export_zone_with_chunks] layout (relatif content) : $LayoutRel"
Write-Host "[export_zone_with_chunks] output : zones/$ZoneId"

& $Zb --layout $LayoutRel --output "zones/$ZoneId" --zone-id $ZoneId --config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Error "zone_builder a échoué (code $LASTEXITCODE)."
}

Write-Host "[export_zone_with_chunks] OK (code 0)."
