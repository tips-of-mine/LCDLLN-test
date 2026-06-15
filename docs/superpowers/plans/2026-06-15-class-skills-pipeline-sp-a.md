# Compétences par-classe — SP-A : pipeline + catalogues — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Générer les données de compétences par-classe (24 classes × 180 skills) depuis la référence externe, et les charger via un catalogue serveur strict + un catalogue client tolérant.

**Architecture:** Un générateur PowerShell déterministe lit la référence externe + la table de mapping + les formules de synthèse → émet 24 fichiers `game/data/gameplay/class_skills/<classId>.json` (commités). Deux catalogues C++ (miroirs de `SpellKitLibrary`/`SpellKitCatalog`) les chargent. SP-A est **purement contenu + données** : aucun changement de combat, aucun cast (le cast reste sur les kits profil jusqu'à SP-C).

**Tech Stack:** PowerShell 5.1 (générateur), C++20 (catalogues, parseur JSON maison local par module), CTest via `lcdlln_add_simple_test`.

**Déviation vs spec §10 (simplification) :** SP-A n'ajoute **aucun** effet moteur. Le catalogue utilise un enum auto-contenu `ClassSkillEffectKind {Damage,Heal,Defense}`. L'effet combat `DamageReductionPercent` + le mapping vers le moteur SP3 sont reportés à **SP-C** (quand le cast est réellement branché). SP-A = zéro impact gameplay, zéro risque combat.

**Livraison : 1 PR** (base main). **Déploiement** : redéploiement shardd (nouveau `ClassSkillLibrary` chargé au boot) mais **inerte** → pas de lock-step.

---

### Task 1: Générateur PowerShell + génération des 24 fichiers de données

**Files:**
- Create: `tools/skills/GenerateClassSkills.ps1`
- Create (généré, commité): `game/data/gameplay/class_skills/<classId>.json` (24 fichiers)

- [ ] **Step 1: Écrire le générateur**

Create `tools/skills/GenerateClassSkills.ps1` :

```powershell
<#
  GenerateClassSkills.ps1 — SP-A. Transforme la reference externe lune-noire
  (skill_trees) en fichiers de competences par-classe pour les classes EXISTANTES.
  Deterministe (aucun horodatage en sortie). Sortie ASCII (translitteration des
  accents ; pas d'icone emoji) pour compat parseur maison + police Windlass.
  Usage : .\GenerateClassSkills.ps1 -ReferencePath <chemin.json> -OutputDir <dir>
#>
param(
  [Parameter(Mandatory=$true)][string]$ReferencePath,
  [string]$OutputDir = "game/data/gameplay/class_skills"
)
$ErrorActionPreference = "Stop"

# Mapping fige : classe jeu -> arbre reference (spec SP-A §6).
$MAP = [ordered]@{
  "guerrier"="class_warrior"; "archer"="class_archer"; "archer_bois"="class_archer";
  "arbaletrier"="class_crossbowman"; "voleur"="class_thief"; "voleur_tenebreux"="class_thief";
  "assassin"="Assassin"; "mage"="class_mage"; "archimage"="Archimage"; "chaman"="class_shaman";
  "paladin"="Paladin"; "pisteur"="Pisteur"; "demoniste"="Démoniste"; "tourmenteur"="Tourmenteur";
  "sorcier_sang"="Sorcier de sang"; "gardien_ecailles"="Gardien d'écailles"; "brise_roc"="Brise-roc";
  "dragonnier"="Dragonnier"; "menthats"="menthats"; "pretre_lune_noire"="class_black_moon_priest";
  "pretre_jugement"="Paladin"; "pretre_grace"="Prêtre"; "inquisiteur_hospitalier"="Inquisiteur";
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
```

- [ ] **Step 2: Exécuter le générateur (produit les 24 fichiers)**

Run (PowerShell, avec le chemin réel de la référence) :
```
.\tools\skills\GenerateClassSkills.ps1 -ReferencePath "D:\Users\thedj\Downloads\lune-noire-data_41_.json"
```
Expected : 24 lignes `OK <classId> (<tree>) -> 180 skills` + `Termine : 24 classes.` ; 24 fichiers créés sous `game/data/gameplay/class_skills/`.

- [ ] **Step 3: Vérifier un fichier généré**

Ouvrir `game/data/gameplay/class_skills/pretre_grace.json` : doit contenir `classId`, `sourceTree:"Prêtre"`, et un tableau `skills` de 180 entrées ; vérifier qu'une entrée `def` a `effectKind:"Defense"`, qu'une entrée `single` a `effectKind:"Heal"` (Prêtre = healer), `resourceCostPercent` ∈ [5,60], noms ASCII (pas d'accent).

- [ ] **Step 4: Commit**

```bash
git add tools/skills/GenerateClassSkills.ps1 game/data/gameplay/class_skills/
git commit -m "feat(data): generateur + donnees competences par-classe (24 classes x 180 skills)"
```

---

### Task 2: `ClassSkillLibrary` serveur (strict) + tests

**Files:**
- Create: `src/shardd/gameplay/spell/ClassSkillLibrary.h`
- Create: `src/shardd/gameplay/spell/ClassSkillLibrary.cpp`
- Test: `src/shardd/gameplay/spell/ClassSkillLibraryTests.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Header**

Create `src/shardd/gameplay/spell/ClassSkillLibrary.h` :

```cpp
#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// SP-A — catégorie d'effet d'une compétence par-classe (auto-contenu ; le
	/// mapping vers le moteur de combat est fait en SP-C, pas ici).
	enum class ClassSkillEffectKind : uint8_t { Damage = 0, Heal = 1, Defense = 2 };

	/// SP-A — type de cible d'une compétence par-classe.
	enum class ClassSkillTarget : uint8_t { SingleEnemy = 0, AreaAroundSelf = 1, SingleAlly = 2 };

	/// SP-A — une compétence par-classe (immuable après chargement).
	struct ClassSkillDef
	{
		std::string skillId;
		std::string name;
		std::string branch;          ///< "single" / "aoe" / "def".
		uint32_t tier = 1;           ///< 1..60.
		uint32_t level = 1;          ///< niveau de déblocage (= tier).
		ClassSkillEffectKind effectKind = ClassSkillEffectKind::Damage;
		ClassSkillTarget target = ClassSkillTarget::SingleEnemy;
		float powerValue = 1.0f;     ///< multiplicateur (≥ 1.0).
		float rangeMeters = 0.0f;
		float areaRadiusMeters = 0.0f;
		uint32_t castTimeMs = 0;
		uint32_t cooldownMs = 0;
		uint32_t resourceCostPercent = 0; ///< [5,60].
		std::string description;
	};

	/// SP-A — bibliothèque serveur des compétences par-classe, résolue depuis
	/// `paths.content` (`gameplay/class_skills/*.json`). Politique STRICTE (pattern
	/// SpellKitLibrary) : fichier illisible/invalide = échec d'Init.
	class ClassSkillLibrary final
	{
	public:
		explicit ClassSkillLibrary(const engine::core::Config& config);

		/// Charge et valide tous les `gameplay/class_skills/*.json`. Idempotent.
		bool Init();

		/// Retourne les skills d'une classe (triés par level), ou nullptr.
		const std::vector<ClassSkillDef>* GetClassSkills(std::string_view classId) const;

		/// Retourne un skill d'une classe, ou nullptr.
		const ClassSkillDef* FindSkill(std::string_view classId, std::string_view skillId) const;

		/// Nombre de classes chargées (0 avant Init).
		size_t ClassCount() const { return m_classes.size(); }

		/// Variante testable sans I/O : parse et valide le JSON d'UNE classe.
		bool LoadClassFromText(std::string_view jsonText, std::string& outError);

	private:
		engine::core::Config m_config;
		std::unordered_map<std::string, std::vector<ClassSkillDef>> m_classes;
		bool m_initialized = false;
	};
}
```

- [ ] **Step 2: Écrire le test d'abord**

Create `src/shardd/gameplay/spell/ClassSkillLibraryTests.cpp` :

```cpp
#include "src/shardd/gameplay/spell/ClassSkillLibrary.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace
{
	using engine::server::ClassSkillLibrary;
	using engine::server::ClassSkillEffectKind;
	using engine::server::ClassSkillTarget;

	const char* kValidClass = R"JSON(
	{
	  "classId": "pretre_grace",
	  "sourceTree": "Pretre",
	  "skills": [
	    { "id": "pretre_grace_single_t1", "name": "Soin", "branch": "single", "tier": 1, "level": 1,
	      "effectKind": "Heal", "target": "SingleAlly", "powerValue": 1.0, "rangeMeters": 6.0,
	      "areaRadiusMeters": 0.0, "castTimeMs": 1000, "cooldownMs": 3000, "resourceCostPercent": 6,
	      "description": "Restaure la vie." },
	    { "id": "pretre_grace_def_t1", "name": "Garde", "branch": "def", "tier": 1, "level": 1,
	      "effectKind": "Defense", "target": "SingleAlly", "powerValue": 1.0, "rangeMeters": 0.0,
	      "areaRadiusMeters": 0.0, "castTimeMs": 0, "cooldownMs": 18000, "resourceCostPercent": 8,
	      "description": "Reduit les degats." }
	  ]
	}
	)JSON";

	void TestLoadValidClass()
	{
		engine::core::Config cfg;
		ClassSkillLibrary lib(cfg);
		std::string err;
		assert(lib.LoadClassFromText(kValidClass, err));
		assert(err.empty());
		const auto* skills = lib.GetClassSkills("pretre_grace");
		assert(skills != nullptr);
		assert(skills->size() == 2u);
		const auto* heal = lib.FindSkill("pretre_grace", "pretre_grace_single_t1");
		assert(heal != nullptr);
		assert(heal->effectKind == ClassSkillEffectKind::Heal);
		assert(heal->target == ClassSkillTarget::SingleAlly);
		const auto* def = lib.FindSkill("pretre_grace", "pretre_grace_def_t1");
		assert(def != nullptr);
		assert(def->effectKind == ClassSkillEffectKind::Defense);
		assert(lib.FindSkill("pretre_grace", "inconnu") == nullptr);
		assert(lib.GetClassSkills("inconnu") == nullptr);
		std::puts("[OK] TestLoadValidClass");
	}

	void TestRejectsBadEffectKind()
	{
		engine::core::Config cfg;
		ClassSkillLibrary lib(cfg);
		std::string err;
		const char* bad = R"JSON({"classId":"x","skills":[{"id":"a","name":"n","branch":"single","tier":1,"level":1,"effectKind":"Bogus","target":"SingleEnemy","powerValue":1.0,"rangeMeters":0.0,"areaRadiusMeters":0.0,"castTimeMs":0,"cooldownMs":3000,"resourceCostPercent":6,"description":""}]})JSON";
		assert(!lib.LoadClassFromText(bad, err));
		assert(!err.empty());
		std::puts("[OK] TestRejectsBadEffectKind");
	}

	void TestRejectsEmptySkills()
	{
		engine::core::Config cfg;
		ClassSkillLibrary lib(cfg);
		std::string err;
		assert(!lib.LoadClassFromText(R"JSON({"classId":"x","skills":[]})JSON", err));
		std::puts("[OK] TestRejectsEmptySkills");
	}
}

int main()
{
	TestLoadValidClass();
	TestRejectsBadEffectKind();
	TestRejectsEmptySkills();
	std::puts("[OK] ClassSkillLibraryTests");
	return 0;
}
```

- [ ] **Step 3: Implémenter `ClassSkillLibrary.cpp`**

Create `src/shardd/gameplay/spell/ClassSkillLibrary.cpp`. **Copier verbatim** le bloc `namespace { ... JsonType / JsonValue / JsonParser / FindObjectMember / TryGetUint / TryGetFloat ... }` depuis `src/shardd/gameplay/spell/SpellKitLibrary.cpp` (parseur maison local). Puis :

```cpp
#include "src/shardd/gameplay/spell/ClassSkillLibrary.h"
#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

// <<< COLLER ICI le bloc anonyme JsonParser/JsonValue/FindObjectMember/TryGetUint/
//     TryGetFloat copié verbatim depuis SpellKitLibrary.cpp >>>

namespace engine::server
{
	namespace
	{
		bool ParseEffectKind(const std::string& s, ClassSkillEffectKind& out)
		{
			if (s == "Damage") { out = ClassSkillEffectKind::Damage; return true; }
			if (s == "Heal") { out = ClassSkillEffectKind::Heal; return true; }
			if (s == "Defense") { out = ClassSkillEffectKind::Defense; return true; }
			return false;
		}
		bool ParseTarget(const std::string& s, ClassSkillTarget& out)
		{
			if (s == "SingleEnemy") { out = ClassSkillTarget::SingleEnemy; return true; }
			if (s == "AreaAroundSelf") { out = ClassSkillTarget::AreaAroundSelf; return true; }
			if (s == "SingleAlly") { out = ClassSkillTarget::SingleAlly; return true; }
			return false;
		}
	}

	ClassSkillLibrary::ClassSkillLibrary(const engine::core::Config& config)
		: m_config(config)
	{
		LOG_INFO(Net, "[ClassSkillLibrary] Constructed");
	}

	const std::vector<ClassSkillDef>* ClassSkillLibrary::GetClassSkills(std::string_view classId) const
	{
		const auto it = m_classes.find(std::string(classId));
		if (it == m_classes.end()) { return nullptr; }
		return &it->second;
	}

	const ClassSkillDef* ClassSkillLibrary::FindSkill(std::string_view classId, std::string_view skillId) const
	{
		const std::vector<ClassSkillDef>* skills = GetClassSkills(classId);
		if (skills == nullptr) { return nullptr; }
		for (const ClassSkillDef& s : *skills)
		{
			if (s.skillId == skillId) { return &s; }
		}
		return nullptr;
	}

	bool ClassSkillLibrary::LoadClassFromText(std::string_view jsonText, std::string& outError)
	{
		JsonParser parser(jsonText);
		JsonValue root;
		if (!parser.Parse(root) || root.type != JsonType::Object)
		{
			outError = "class_skills: JSON racine invalide";
			return false;
		}
		const JsonValue* classIdValue = FindObjectMember(root, "classId");
		if (classIdValue == nullptr || classIdValue->type != JsonType::String || classIdValue->stringValue.empty())
		{
			outError = "class_skills: 'classId' manquant";
			return false;
		}
		const std::string classId = classIdValue->stringValue;

		const JsonValue* skillsValue = FindObjectMember(root, "skills");
		if (skillsValue == nullptr || skillsValue->type != JsonType::Array || skillsValue->arrayValue.empty())
		{
			outError = "class_skills: 'skills' vide ou absent";
			return false;
		}

		std::vector<ClassSkillDef> skills;
		skills.reserve(skillsValue->arrayValue.size());
		for (const JsonValue& entry : skillsValue->arrayValue)
		{
			if (entry.type != JsonType::Object) { outError = "class_skills: entrée non-objet"; return false; }
			ClassSkillDef def{};
			const JsonValue* idV = FindObjectMember(entry, "id");
			const JsonValue* nameV = FindObjectMember(entry, "name");
			const JsonValue* branchV = FindObjectMember(entry, "branch");
			const JsonValue* effV = FindObjectMember(entry, "effectKind");
			const JsonValue* tgtV = FindObjectMember(entry, "target");
			const JsonValue* descV = FindObjectMember(entry, "description");
			if (idV == nullptr || idV->type != JsonType::String || idV->stringValue.empty()
				|| nameV == nullptr || nameV->type != JsonType::String
				|| branchV == nullptr || branchV->type != JsonType::String
				|| effV == nullptr || effV->type != JsonType::String
				|| tgtV == nullptr || tgtV->type != JsonType::String)
			{
				outError = "class_skills: champ string manquant";
				return false;
			}
			def.skillId = idV->stringValue;
			def.name = nameV->stringValue;
			def.branch = branchV->stringValue;
			def.description = (descV != nullptr && descV->type == JsonType::String) ? descV->stringValue : "";
			if (!ParseEffectKind(effV->stringValue, def.effectKind)) { outError = "class_skills: effectKind invalide"; return false; }
			if (!ParseTarget(tgtV->stringValue, def.target)) { outError = "class_skills: target invalide"; return false; }

			const JsonValue* tierV = FindObjectMember(entry, "tier");
			const JsonValue* levelV = FindObjectMember(entry, "level");
			const JsonValue* castV = FindObjectMember(entry, "castTimeMs");
			const JsonValue* cdV = FindObjectMember(entry, "cooldownMs");
			const JsonValue* costV = FindObjectMember(entry, "resourceCostPercent");
			if (tierV == nullptr || !TryGetUint(*tierV, def.tier)
				|| levelV == nullptr || !TryGetUint(*levelV, def.level)
				|| castV == nullptr || !TryGetUint(*castV, def.castTimeMs)
				|| cdV == nullptr || !TryGetUint(*cdV, def.cooldownMs)
				|| costV == nullptr || !TryGetUint(*costV, def.resourceCostPercent)
				|| def.resourceCostPercent > 100u)
			{
				outError = "class_skills: champ entier invalide";
				return false;
			}

			const JsonValue* powV = FindObjectMember(entry, "powerValue");
			const JsonValue* rngV = FindObjectMember(entry, "rangeMeters");
			const JsonValue* radV = FindObjectMember(entry, "areaRadiusMeters");
			if (powV == nullptr || !TryGetFloat(*powV, def.powerValue) || def.powerValue < 1.0f
				|| rngV == nullptr || !TryGetFloat(*rngV, def.rangeMeters) || def.rangeMeters < 0.0f
				|| radV == nullptr || !TryGetFloat(*radV, def.areaRadiusMeters) || def.areaRadiusMeters < 0.0f)
			{
				outError = "class_skills: champ flottant invalide";
				return false;
			}
			skills.push_back(std::move(def));
		}

		std::sort(skills.begin(), skills.end(),
			[](const ClassSkillDef& a, const ClassSkillDef& b) { return a.level < b.level; });
		m_classes[classId] = std::move(skills);
		return true;
	}

	bool ClassSkillLibrary::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[ClassSkillLibrary] Init ignored: already initialized");
			return true;
		}
		// Même mécanisme de scan que SpellKitLibrary::Init (FileSystem::ListDirectory
		// + ReadAllTextContent), mais sur le dossier "gameplay/class_skills".
		std::vector<std::string> files;
		if (!engine::platform::FileSystem::ListContentDirectory(m_config, "gameplay/class_skills", ".json", files))
		{
			LOG_ERROR(Net, "[ClassSkillLibrary] Init FAILED: cannot list gameplay/class_skills");
			return false;
		}
		for (const std::string& relativePath : files)
		{
			std::string text;
			if (!engine::platform::FileSystem::ReadAllTextContent(m_config, relativePath, text))
			{
				LOG_ERROR(Net, "[ClassSkillLibrary] Init FAILED: cannot read {}", relativePath);
				m_classes.clear();
				return false;
			}
			std::string err;
			if (!LoadClassFromText(text, err))
			{
				LOG_ERROR(Net, "[ClassSkillLibrary] Init FAILED: {} ({})", relativePath, err);
				m_classes.clear();
				return false;
			}
		}
		if (m_classes.empty())
		{
			LOG_ERROR(Net, "[ClassSkillLibrary] Init FAILED: no class skills loaded");
			return false;
		}
		m_initialized = true;
		LOG_INFO(Net, "[ClassSkillLibrary] Init OK ({} classes)", m_classes.size());
		return true;
	}
}
```

> **Note** : aligner les noms exacts des helpers `FileSystem` (`ListContentDirectory`/`ReadAllTextContent`/`ResolveContentPath`) sur ceux réellement utilisés par `SpellKitLibrary::Init` — ouvrir `SpellKitLibrary.cpp` et recopier le MÊME mécanisme de scan (les noms ci-dessus sont indicatifs). Ajouter `#include <algorithm>` pour `std::sort`.

- [ ] **Step 4: Enregistrer dans CMake**

Dans `src/CMakeLists.txt` : ajouter `ClassSkillLibrary.cpp` aux **deux** listes serveur, à côté de `SpellKitLibrary.cpp` (cible `server_app` Windows ~l.75 ET `shard_app` Linux ~l.1187) :
```cmake
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/ClassSkillLibrary.cpp
```
Et la cible de test à côté de `spell_kit_library_tests` (~l.545) :
```cmake
  lcdlln_add_simple_test(class_skill_library_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/ClassSkillLibraryTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/spell/ClassSkillLibrary.cpp)
```

- [ ] **Step 5: Commit**

```bash
git add src/shardd/gameplay/spell/ClassSkillLibrary.h src/shardd/gameplay/spell/ClassSkillLibrary.cpp src/shardd/gameplay/spell/ClassSkillLibraryTests.cpp src/CMakeLists.txt
git commit -m "feat(server): ClassSkillLibrary (catalogue strict competences par-classe) + tests"
```

---

### Task 3: `ClassSkillCatalog` client (tolérant) + tests

**Files:**
- Create: `src/client/gameplay/ClassSkillCatalog.h`
- Create: `src/client/gameplay/ClassSkillCatalog.cpp`
- Test: `src/client/gameplay/ClassSkillCatalogTests.cpp`
- Modify: `CMakeLists.txt` (racine) + `src/CMakeLists.txt`

- [ ] **Step 1: Header**

Create `src/client/gameplay/ClassSkillCatalog.h` :

```cpp
#pragma once

#include "src/shared/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// SP-A — métadonnées d'affichage d'une compétence par-classe (client). Le
	/// client ne lit QUE l'affichage ; la résolution des effets reste serveur (SP-C).
	struct ClassSkillDisplay
	{
		std::string skillId;
		std::string name;
		std::string branch;           ///< "single" / "aoe" / "def".
		uint32_t tier = 1;
		uint32_t level = 1;
		std::string effectKind;       ///< "Damage" / "Heal" / "Defense" (brut).
		std::string target;           ///< "SingleEnemy" / "AreaAroundSelf" / "SingleAlly".
		float powerValue = 1.0f;
		float rangeMeters = 0.0f;
		float areaRadiusMeters = 0.0f;
		uint32_t castTimeMs = 0;
		uint32_t cooldownMs = 0;
		uint32_t resourceCostPercent = 0;
		std::string description;
	};

	/// SP-A — catalogue client des compétences par-classe (mêmes fichiers
	/// `gameplay/class_skills/*.json`). Politique TOLÉRANTE : fichier absent/invalide
	/// = catalogue vide + LOG_WARN (le serveur reste autorité).
	class ClassSkillCatalog final
	{
	public:
		/// Charge tous les `gameplay/class_skills/*.json`. Retourne false si rien
		/// n'est lisible (non bloquant).
		bool Init(const engine::core::Config& config);

		/// Retourne les skills d'une classe (triés par level), ou nullptr.
		const std::vector<ClassSkillDisplay>* GetClassSkills(std::string_view classId) const;

		/// Nombre de classes chargées (0 si vide).
		size_t ClassCount() const { return m_classes.size(); }

		/// Variante testable sans I/O : parse le JSON d'UNE classe.
		bool LoadClassFromText(std::string_view jsonText, std::string& outError);

	private:
		std::unordered_map<std::string, std::vector<ClassSkillDisplay>> m_classes;
	};
}
```

- [ ] **Step 2: Écrire le test d'abord**

Create `src/client/gameplay/ClassSkillCatalogTests.cpp` :

```cpp
#include "src/client/gameplay/ClassSkillCatalog.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace
{
	const char* kClass = R"JSON(
	{ "classId": "mage", "sourceTree": "class_mage", "skills": [
	  { "id": "mage_single_t1", "name": "Eclair", "branch": "single", "tier": 1, "level": 1,
	    "effectKind": "Damage", "target": "SingleEnemy", "powerValue": 1.1, "rangeMeters": 30.0,
	    "areaRadiusMeters": 0.0, "castTimeMs": 1500, "cooldownMs": 3000, "resourceCostPercent": 6,
	    "description": "Decharge arcanique." } ] }
	)JSON";

	void TestLoadClass()
	{
		engine::client::ClassSkillCatalog cat;
		std::string err;
		assert(cat.LoadClassFromText(kClass, err));
		const auto* s = cat.GetClassSkills("mage");
		assert(s != nullptr && s->size() == 1u);
		assert((*s)[0].effectKind == "Damage");
		assert((*s)[0].rangeMeters == 30.0f);
		assert(cat.GetClassSkills("inconnu") == nullptr);
		std::puts("[OK] TestLoadClass");
	}
}

int main()
{
	TestLoadClass();
	std::puts("[OK] ClassSkillCatalogTests");
	return 0;
}
```

- [ ] **Step 3: Implémenter `ClassSkillCatalog.cpp`**

Create `src/client/gameplay/ClassSkillCatalog.cpp`. **Copier verbatim** le bloc anonyme `JsonParser` depuis `src/client/gameplay/SpellKitCatalog.cpp`. Puis implémenter `Init` (tolérant : scan `gameplay/class_skills`, `LOG_WARN` + `continue` sur fichier invalide, retourne `m_classes` non vide), `GetClassSkills` (identique au Library), et `LoadClassFromText` (même parsing que `ClassSkillLibrary::LoadClassFromText` Task 2 Step 3, mais en remplissant un `ClassSkillDisplay` — `effectKind`/`target` restent des **strings brutes**, pas d'enum ; pas de rejet sur valeur d'enum inconnue ; trie par level). Aligner le mécanisme de scan/lecture fichier sur `SpellKitCatalog::Init`.

- [ ] **Step 4: CMake**

Dans `CMakeLists.txt` (racine), ajouter à `engine_core` à côté de `SpellKitCatalog.cpp` (~l.682) :
```cmake
  src/client/gameplay/ClassSkillCatalog.cpp
```
Dans `src/CMakeLists.txt`, cible de test à côté de `action_bar_layout_tests` (NE PAS re-lister `ClassSkillCatalog.cpp` — déjà dans engine_core) :
```cmake
  lcdlln_add_simple_test(class_skill_catalog_tests
    ${CMAKE_SOURCE_DIR}/src/client/gameplay/ClassSkillCatalogTests.cpp)
```

- [ ] **Step 5: Commit**

```bash
git add src/client/gameplay/ClassSkillCatalog.h src/client/gameplay/ClassSkillCatalog.cpp src/client/gameplay/ClassSkillCatalogTests.cpp CMakeLists.txt src/CMakeLists.txt
git commit -m "feat(client): ClassSkillCatalog (catalogue tolerant competences par-classe) + tests"
```

---

### Task 4: CODEBASE_MAP + PR

**Files:**
- Modify: `CODEBASE_MAP.md`

- [ ] **Step 1: Documenter**

Ajouter une entrée « Compétences par-classe SP-A » : générateur `tools/skills/GenerateClassSkills.ps1` (référence externe → `game/data/gameplay/class_skills/*.json`), `ClassSkillLibrary` (serveur strict) + `ClassSkillCatalog` (client tolérant), inertes jusqu'à SP-C. Noter le mapping 24 classes (spec).

- [ ] **Step 2: Commit + PR**

```bash
git add CODEBASE_MAP.md
git commit -m "docs(codebase-map): competences par-classe SP-A"
git push -u origin <branche>
```
Ouvrir la PR base `main`. Description : ⚠️ redéploiement **shardd** (nouveau `ClassSkillLibrary` au boot) mais **inerte** (cast inchangé jusqu'à SP-C) → pas de lock-step.

---

## Self-review

- **Couverture spec** : §6 mapping → Task 1 table `$MAP` ; §7 générateur externe → Task 1 ; §8 schéma sortie → Task 1 (objet émis) + Task 2/3 (parsing) ; §9 formules → Task 1 (constantes) ; §10.2 catalogues → Task 2 (serveur strict) + Task 3 (client tolérant) ; §11 tests → Tasks 2/3 ; §12 déploiement → Task 4. **Déviation §10.1** (effet moteur `DamageReductionPercent` reporté à SP-C) documentée en tête — SP-A utilise `ClassSkillEffectKind` auto-contenu.
- **Placeholders** : aucun « TBD ». Les 2 instructions « copier verbatim le bloc JsonParser » et « aligner les noms FileSystem sur SpellKitLibrary » désignent un bloc existant précis (pas un placeholder), car ce parseur est dupliqué par module (convention repo confirmée).
- **Cohérence des types** : `ClassSkillDef`/`ClassSkillEffectKind`/`ClassSkillTarget` (serveur) ; `ClassSkillDisplay` (client, strings brutes) ; champs JSON identiques entre générateur (Task 1) et parsers (Tasks 2/3) : `id,name,branch,tier,level,effectKind,target,powerValue,rangeMeters,areaRadiusMeters,castTimeMs,cooldownMs,resourceCostPercent,description`.
- **CMake** : `ClassSkillLibrary.cpp` (serveur, 2 listes) listé dans son test ; `ClassSkillCatalog.cpp` (engine_core) PAS re-listé dans son test (anti double-link).
