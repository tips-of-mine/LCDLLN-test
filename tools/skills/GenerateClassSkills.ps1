<#
  GenerateClassSkills.ps1 - SP-A. Transforme la reference externe lune-noire
  (skill_trees) en fichiers de competences par-classe pour les classes EXISTANTES.
  Deterministe (aucun horodatage en sortie). Sortie ASCII (translitteration des
  accents ; pas d'icone emoji) pour compat parseur maison + police Windlass.
  Usage : .\GenerateClassSkills.ps1 -ReferencePath <chemin.json> -OutputDir <dir>
  Note : les class_id de la reference contenant des accents sont construits via
  [char] pour eviter les problemes d'encodage source sous PowerShell 5.1.
#>
param(
  [Parameter(Mandatory=$true)][string]$ReferencePath,
  [string]$OutputDir = "game/data/gameplay/class_skills"
)
$ErrorActionPreference = "Stop"

# Caracteres accentues construits programmatiquement (compat PowerShell 5.1 UTF-16).
$e_aigu    = [char]0x00E9  # e avec accent aigu
$e_grave   = [char]0x00E8  # e avec accent grave
$e_circ    = [char]0x00EA  # e avec accent circonflexe
$i_circ    = [char]0x00EE  # i avec accent circonflexe (non utilise)
$apos_haut = [char]0x0027  # apostrophe simple

# class_id de la reference qui contiennent des accents.
# Verification : "Demoniste" = D + e_aigu (0xE9) + moniste
#               "Gardien d'ecailles" = ... d + apos + e_aigu + cailles
#               "Pretre" = Pr + e_circ (0xEA) + tre
$id_Demoniste       = "D${e_aigu}moniste"
$id_GardienEcailles = "Gardien d${apos_haut}${e_aigu}cailles"
$id_Pretre          = "Pr${e_circ}tre"
$id_Inquisiteur     = "Inquisiteur"
$id_SorcierSang     = "Sorcier de sang"

# Mapping fige : classe jeu -> arbre reference (spec SP-A §6).
$MAP = [ordered]@{
  "guerrier"="class_warrior"; "archer"="class_archer"; "archer_bois"="class_archer";
  "arbaletrier"="class_crossbowman"; "voleur"="class_thief"; "voleur_tenebreux"="class_thief";
  "assassin"="Assassin"; "mage"="class_mage"; "archimage"="Archimage"; "chaman"="class_shaman";
  "paladin"="Paladin"; "pisteur"="Pisteur";
  "demoniste"=$id_Demoniste; "tourmenteur"="Tourmenteur";
  "sorcier_sang"=$id_SorcierSang; "gardien_ecailles"=$id_GardienEcailles; "brise_roc"="Brise-roc";
  "dragonnier"="Dragonnier"; "menthats"="menthats"; "pretre_lune_noire"="class_black_moon_priest";
  "pretre_jugement"="Paladin"; "pretre_grace"=$id_Pretre; "inquisiteur_hospitalier"=$id_Inquisiteur;
  "inquisiteur_chatieur"="Paladin"
}

# Translittere accents -> ASCII (compat parseur maison + Windlass).
function To-Ascii([string]$s) {
  if ($null -eq $s) { return "" }
  $norm = $s.Normalize([Text.NormalizationForm]::FormD)
  $sb = New-Object Text.StringBuilder
  foreach ($c in $norm.ToCharArray()) {
    if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($c) -ne [Globalization.UnicodeCategory]::NonSpacingMark) {
      [void]$sb.Append($c)
    }
  }
  # Retire tout caractere non-ASCII restant (emoji, etc.).
  return ($sb.ToString().Normalize([Text.NormalizationForm]::FormC) -replace '[^\x00-\x7F]', '')
}

function Clamp([double]$v,[double]$lo,[double]$hi) { [Math]::Max($lo,[Math]::Min($hi,$v)) }

$ref = Get-Content -Raw -Encoding UTF8 $ReferencePath | ConvertFrom-Json
$treesById = @{}
foreach ($t in $ref.skill_trees) { $treesById[$t.class_id] = $t }

if (-not (Test-Path $OutputDir)) { New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null }

foreach ($classId in $MAP.Keys) {
  $treeId = $MAP[$classId]
  $tree = $treesById[$treeId]
  if ($null -eq $tree) { throw "Arbre introuvable pour $classId -> $treeId" }

  $outSkills = New-Object Collections.ArrayList
  foreach ($sk in ($tree.skills | Sort-Object level, branch_id)) {
    $branch = [string]$sk.branch_id          # single / aoe / def
    $kindRaw = [string]$sk.power_kind         # damage / heal / defense
    $effectKind = switch ($kindRaw) { "damage" {"Damage"} "heal" {"Heal"} "defense" {"Defense"} default { throw "power_kind inconnu: $kindRaw" } }
    $tgtRaw = [string]$sk.target_type         # unitaire / zone / soi_allie
    $target = switch ($tgtRaw) {
      "unitaire" { if ($effectKind -eq "Heal") { "SingleAlly" } else { "SingleEnemy" } }
      "zone" { "AreaAroundSelf" }
      "soi_allie" { "SingleAlly" }
      default { throw "target_type inconnu: $tgtRaw" }
    }
    $power = [double]$sk.power_value
    $rangeM = [double]$sk.range_m
    $radiusM = [double]$sk.radius_m
    $weaponRel = [bool]$sk.range_weapon_relative

    # Formules de synthese (spec SP-A §9).
    $baseCost = switch ($branch) { "single" {6} "aoe" {10} "def" {8} default {8} }
    $cost = [int](Clamp ($baseCost + [Math]::Round(($power - 1.0) * 4)) 5 60)
    $cooldown = switch ($branch) { "single" {3000} "aoe" {10000} "def" {18000} default {10000} }
    $cast = 0
    if ($effectKind -eq "Damage") { if (($rangeM -gt 5.0) -and (-not $weaponRel)) { $cast = 1500 } else { $cast = 0 } }
    elseif ($effectKind -eq "Heal") { $cast = 1000 }
    else { $cast = 0 }

    [void]$outSkills.Add([ordered]@{
      id = "$($classId)_$($branch)_t$($sk.tier)"
      name = To-Ascii $sk.name
      branch = $branch
      tier = [int]$sk.tier
      level = [int]$sk.level
      effectKind = $effectKind
      target = $target
      powerValue = [double]$power
      rangeMeters = [double]$rangeM
      areaRadiusMeters = [double]$radiusM
      castTimeMs = [int]$cast
      cooldownMs = [int]$cooldown
      resourceCostPercent = [int]$cost
      description = To-Ascii $sk.description
    })
  }

  $doc = [ordered]@{ classId = $classId; sourceTree = $treeId; skills = $outSkills }
  $json = $doc | ConvertTo-Json -Depth 6
  $outPath = Join-Path $OutputDir "$classId.json"
  [IO.File]::WriteAllText((Resolve-Path -LiteralPath (Split-Path $outPath -Parent)).Path + "\$classId.json", $json, (New-Object Text.UTF8Encoding($false)))
  Write-Output "OK $classId ($treeId) -> $($outSkills.Count) skills"
}
Write-Output "Termine : $($MAP.Count) classes."
