# Système de Personnages — PR1 (server-first) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Livrer le moteur de stats serveur (11 stats dérivées, déterministe, anti-triche par embarquement au build), la nouvelle courbe d'XP, et l'extension réplication `Unit`/`Player` — le tout testé et autonome, sans toucher au wire ni au client.

**Architecture:** `shardd` calcule les 11 stats à partir de `(level, classId, factionId, gender)` et de tables **embarquées dans le binaire au build** (header C++ généré par CMake depuis `game/data/`). Le moteur (`CharacterStatsEngine`) est pur et compilé **uniquement** dans `shardd` (les multiplicateurs ne fuient jamais au client). L'extension `Unit`/`Player` ajoute les `UpdateField` de réplication delta. La taxonomie non secrète (factions/classes) vit dans `factions.json` (lisible client en PR2) ; les multiplicateurs secrets dans `character_stats.json`.

**Tech Stack:** C++20, CMake (codegen via `cmake -P`), `engine::core::Config` (parseur JSON interne `JsonParser`/`MergeJsonFlatten`), tests « plain main() → 0/1 » enregistrés via `add_test`, CI GitHub (build-linux exécute ctest).

---

## Périmètre & reports (lire avant de commencer)

**Dans PR1 :**
- `game/data/races/factions.json` (NOUVEAU, taxonomie non secrète).
- `game/data/gameplay/character_stats.json` (NOUVEAU, multiplicateurs — embarqué shardd).
- Migration `0072_factions_v2.sql` (×2 arbres : `sql/migrations/` et `deploy/docker/sql/migrations/`).
- `Config::LoadFromString` (parse JSON en mémoire, sans disque).
- Codegen CMake (JSON → header `const`) + embarquement dans `server_app`.
- `CharacterStatsTables` (parse les tables embarquées) + `CharacterStatsEngine` (calcul).
- `Formulas.h` : nouvelle courbe XP paramétrée + `FormulasTests` mis à jour.
- Extension `Unit`/`Player` + `UpdateFieldIndices.h` + `Player::ApplyDerivedStats`.
- Tests serveur (ctest Linux).

**Reporté en PR2 (client) — NE PAS toucher ici :**
- `races.json` (flags `enabled`, suppression `corrompus`) → pairé avec la refonte UI et les tests client (`RaceDefinitionTests`, `CharacterCustomizationTests`) pour éviter de casser la CI Windows depuis une PR serveur.
- Wire `factionId` (`CharacterPayloads`), `CharacterCreateHandler`, refonte `CharacterCreationUi`, localisation `faction.*`/`class.*`.

**Différé — décision R1 (hors PR1, à trancher ensuite) :**
- **Rendre les stats visibles au client live.** Constat d'audit : `entities::Player` n'est instancié qu'en tests ; la santé live passe par le chemin ECS `StatsComponent`/`SpawnEntity` (`src/shared/network/ReplicationTypes.h`), pas par `Unit`+`UpdateField`. PR1 livre le moteur + l'entité + les tests (capacité prouvée). Le câblage du moteur vers le chemin de réplication réellement consommé par le client est une décision séparée (étendre le snapshot ECS, ou adopter `Unit`/`Player` pour les joueurs live). **Ne pas l'implémenter dans PR1.**

**Branche :** créer `feat/character-system-pr1-server` depuis `main`. Le spec de référence est `docs/superpowers/specs/2026-06-08-character-system-factions-races-classes-stats-design.md` (valeurs §6.3 = source de vérité).

---

## File Structure

| Fichier | Rôle |
|---------|------|
| `game/data/races/factions.json` (créer) | Taxonomie : 10 factions, classes (id/nom/sous-classe/profil/ressource), `selectable`. Non secret. |
| `game/data/gameplay/character_stats.json` (créer) | Multiplicateurs : bases, `class_profiles`, `race_profiles`, `sex_profiles`, XP, cap crit. **Embarqué shardd**. |
| `sql/migrations/0072_factions_v2.sql` + `deploy/docker/sql/migrations/0072_factions_v2.sql` (créer) | Aligne la table `factions` sur les ids courts + backfill. |
| `cmake/EmbedJson.cmake` (créer) | Script `cmake -P` : fichier JSON → header `const char*` (raw string). |
| `src/shared/core/Config.h` / `Config.cpp` (modifier) | Ajout `LoadFromString` (parse en mémoire). |
| `src/shared/core/ConfigLoadFromStringTests.cpp` (créer) | Test du nouveau parseur en mémoire. |
| `src/shardd/gameplay/character/CharacterStatsTables.{h,cpp}` (créer) | Charge les tables depuis les JSON embarqués (via `Config::LoadFromString`). |
| `src/shardd/gameplay/character/CharacterStatsEngine.{h,cpp}` (créer) | Calcul déterministe des 11 stats. Compilé shardd-only. |
| `src/shardd/gameplay/character/CharacterStatsEngineTests.cpp` (créer) | Tests moteur (anchors + invariants + round-trip). |
| `src/shared/formulas/Formulas.h` (modifier) | Remplace `XpToNextLevel` par version paramétrée. |
| `src/shared/formulas/FormulasTests.cpp` (modifier) | MAJ tests XP. |
| `src/shardd/entities/UpdateFieldIndices.h` (modifier) | Nouveaux indices Unit (appended). |
| `src/shardd/entities/Unit.h` (modifier) | Nouveaux `UpdateField` + accesseurs. |
| `src/shardd/entities/UnitTests.cpp` (modifier) | Tests nouveaux champs. |
| `src/shardd/entities/Player.h` / `Player.cpp` (modifier) | `ApplyDerivedStats`. |
| `src/shardd/entities/PlayerTests.cpp` (modifier) | Test `ApplyDerivedStats`. |
| `src/CMakeLists.txt` (modifier) | Sources + codegen + tests (branches Win **et** Linux de `server_app`). |

---

## Task 1: Branche de travail

**Files:** (aucun fichier code)

- [ ] **Step 1: Créer la branche depuis main**

```bash
git fetch origin
git checkout main
git pull --ff-only
git checkout -b feat/character-system-pr1-server
```

- [ ] **Step 2: Vérifier l'état propre**

Run: `git status`
Expected: `On branch feat/character-system-pr1-server` / `nothing to commit, working tree clean`

---

## Task 2: `Config::LoadFromString` (parse JSON en mémoire)

Le serveur doit parser les tables **embarquées** sans lire le disque. On factorise le chemin JSON de `LoadFromFile` dans une méthode publique réutilisable.

**Files:**
- Modify: `src/shared/core/Config.h`
- Modify: `src/shared/core/Config.cpp:530-569`
- Test: `src/shared/core/ConfigLoadFromStringTests.cpp` (créer)

- [ ] **Step 1: Écrire le test qui échoue**

Créer `src/shared/core/ConfigLoadFromStringTests.cpp` :

```cpp
// Test de Config::LoadFromString : parsing JSON depuis un buffer mémoire
// (aucun accès disque). Sert l'embarquement anti-triche (tables compilées
// dans le binaire shardd, lues via LoadFromString au boot).
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <string>

namespace
{
	using engine::core::Config;

	bool TestParsesFlatObject()
	{
		Config cfg;
		const std::string json = R"({ "a": { "b": 42 }, "name": "x" })";
		if (!cfg.LoadFromString(json)) return false;
		if (cfg.GetInt("a.b", -1) != 42) return false;
		if (cfg.GetString("name", "") != "x") return false;
		return true;
	}

	bool TestParsesArrayIndices()
	{
		Config cfg;
		const std::string json = R"({ "items": [ { "id": "u" }, { "id": "v" } ] })";
		if (!cfg.LoadFromString(json)) return false;
		if (!cfg.Has("items[0].id")) return false;
		if (cfg.GetString("items[0].id", "") != "u") return false;
		if (cfg.GetString("items[1].id", "") != "v") return false;
		if (cfg.Has("items[2].id")) return false;
		return true;
	}

	bool TestRejectsNonObjectAndGarbage()
	{
		Config a; if (a.LoadFromString("not json")) return false;
		Config b; if (b.LoadFromString("[1,2,3]")) return false; // racine non-objet
		return true;
	}
}

int main()
{
	engine::core::LogSettings s; s.level = engine::core::LogLevel::Info; s.console = true;
	engine::core::Log::Init(s);
	const bool ok = TestParsesFlatObject() && TestParsesArrayIndices() && TestRejectsNonObjectAndGarbage();
	if (ok) LOG_INFO(Core, "[ConfigLoadFromStringTests] ALL OK");
	else    LOG_ERROR(Core, "[ConfigLoadFromStringTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 2: Déclarer la méthode dans le header**

Dans `src/shared/core/Config.h`, après la déclaration de `LoadFromFile` (ligne 51) :

```cpp
		/// Parse un document JSON depuis un buffer mémoire (aucun accès disque).
		/// Même sémantique que LoadFromFile pour du JSON : aplatit les objets en
		/// clés pointées, les tableaux en indices `[i]`. Retourne false si le
		/// texte n'est pas un JSON dont la racine est un objet.
		/// Effet : fusionne les valeurs (priorité sur les défauts), comme LoadFromFile.
		bool LoadFromString(std::string_view jsonText);
```

- [ ] **Step 3: Implémenter en factorisant le parseur JSON existant**

Dans `src/shared/core/Config.cpp`, remplacer le bloc JSON de `LoadFromFile` (lignes 543-568, à partir du commentaire `// Default to JSON...`) par un appel à la nouvelle méthode, puis ajouter `LoadFromString` juste après `LoadFromFile`.

Remplacer (dans `LoadFromFile`) :

```cpp
		// Default to JSON when extension is unknown.
		std::stringstream ss;
		ss << in.rdbuf();
		const std::string text = ss.str();

		JsonValue root;
		JsonParser parser(text);
		if (!parser.Parse(root))
		{
			return false;
		}
		if (root.type != JsonValue::Type::Object)
		{
			return false;
		}

		// Merge JSON into config by overriding defaults (file has higher priority than defaults).
		// We flatten nested objects into dotted keys.
		Config fileCfg;
		MergeJsonFlatten(root, "", fileCfg);
		for (const auto& [k, v] : fileCfg.m_values)
		{
			SetValue(k, v);
		}

		return true;
	}
```

par :

```cpp
		// Default to JSON when extension is unknown : on délègue au parseur mémoire.
		std::stringstream ss;
		ss << in.rdbuf();
		return LoadFromString(ss.str());
	}

	bool Config::LoadFromString(std::string_view jsonText)
	{
		JsonValue root;
		JsonParser parser(std::string(jsonText));
		if (!parser.Parse(root))
		{
			return false;
		}
		if (root.type != JsonValue::Type::Object)
		{
			return false;
		}

		// Merge JSON into config by overriding defaults (file has higher priority than defaults).
		// On aplatit les objets imbriqués en clés pointées.
		Config fileCfg;
		MergeJsonFlatten(root, "", fileCfg);
		for (const auto& [k, v] : fileCfg.m_values)
		{
			SetValue(k, v);
		}

		return true;
	}
```

- [ ] **Step 4: Enregistrer le test dans CMake**

Dans `src/CMakeLists.txt`, après le bloc d'un test existant (ex. `session_character_map_tests`, ~ligne 234-241), ajouter (zone Linux, près des autres `add_test`) :

```cmake
  add_executable(config_loadfromstring_tests
    ${CMAKE_SOURCE_DIR}/src/shared/core/ConfigLoadFromStringTests.cpp)
  target_include_directories(config_loadfromstring_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(config_loadfromstring_tests PRIVATE engine_core spdlog::spdlog)
  target_compile_options(config_loadfromstring_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME config_loadfromstring_tests COMMAND config_loadfromstring_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

> Vérifier le nom exact de la lib qui contient `Config`/`Log` (probablement `engine_core`). Si `Config.cpp` est dans une autre cible, lier celle-ci. Inspecter `src/shared/CMakeLists.txt` pour confirmer.

- [ ] **Step 5: Commit**

```bash
git add src/shared/core/Config.h src/shared/core/Config.cpp src/shared/core/ConfigLoadFromStringTests.cpp src/CMakeLists.txt
git commit -m "feat(config): LoadFromString — parse JSON en mémoire (anti-triche embedding)"
```

> CI : pousser et vérifier `build-linux` (ctest) vert sur `config_loadfromstring_tests`. Pas de build local (cf. spec D7).

---

## Task 3: Données — `character_stats.json` (multiplicateurs, embarqué)

Recopier les **valeurs §6.3 verbatim**. Ce fichier ne contient **que** les nombres (pas les noms factions/classes → ceux-ci vont dans `factions.json`, Task 4).

**Files:**
- Create: `game/data/gameplay/character_stats.json`

- [ ] **Step 1: Créer le fichier**

```json
{
  "schema_version": 1,
  "level_max": 100,
  "xp": { "formula": "base * level^factor", "factor": 2.6, "base": 6.185, "xp_per_hour_ref": 10000, "calibration": "niveau 60 ~= 420h de jeu" },
  "bases": {
    "hp":        { "lvl1": 100, "lvl100": 4000 },
    "resource":  { "lvl1": 50,  "lvl100": 2000 },
    "damage":    { "lvl1": 10,  "lvl100": 400 },
    "accuracy":  { "lvl1": 70,  "lvl100": 95 },
    "range":     { "lvl1": 20,  "lvl100": 45 },
    "crit_rate": { "lvl1": 2,   "lvl100": 10, "cap": 10 },
    "crit_mult": { "base": 1.5 },
    "speed_walk": 2, "speed_run": 5, "speed_sprint": 8,
    "stamina":   { "lvl1": 100, "lvl100": 1000 },
    "stamina_cost_run_pct": 4, "stamina_cost_sprint_pct": 8,
    "stamina_regen_t1_pct": 4, "stamina_regen_t2_pct": 7, "stamina_regen_t3_pct": 10,
    "stamina_regen_idle_mult": 1.5,
    "perception_lvl1": 10, "perception_per_level": 0.5
  },
  "class_profiles": {
    "tank":     { "hp": 1.30, "resource": 0.80, "damage": 0.70, "accuracy": 1.00, "range": 0.00, "crit_rate": 0.40, "crit_mult": 0.90, "speed": 0.85, "perception": 0.80, "stealth": 0.40 },
    "melee":    { "hp": 1.15, "resource": 1.00, "damage": 1.05, "accuracy": 1.00, "range": 0.00, "crit_rate": 0.70, "crit_mult": 1.00, "speed": 1.00, "perception": 0.90, "stealth": 0.70 },
    "sacre":    { "hp": 1.10, "resource": 1.00, "damage": 1.00, "accuracy": 1.00, "range": 0.00, "crit_rate": 0.70, "crit_mult": 1.00, "speed": 1.00, "perception": 0.95, "stealth": 0.70 },
    "distance": { "hp": 0.90, "resource": 1.10, "damage": 1.15, "accuracy": 1.00, "range": 1.00, "crit_rate": 0.85, "crit_mult": 1.05, "speed": 1.00, "perception": 1.30, "stealth": 0.90 },
    "pisteur":  { "hp": 0.95, "resource": 1.10, "damage": 1.10, "accuracy": 1.00, "range": 0.90, "crit_rate": 0.80, "crit_mult": 1.05, "speed": 1.10, "perception": 1.25, "stealth": 1.20 },
    "voleur":   { "hp": 0.90, "resource": 1.10, "damage": 1.25, "accuracy": 1.00, "range": 0.30, "crit_rate": 1.00, "crit_mult": 1.20, "speed": 1.15, "perception": 1.00, "stealth": 1.40 },
    "healer":   { "hp": 0.85, "resource": 1.20, "damage": 0.65, "accuracy": 1.00, "range": 0.70, "crit_rate": 0.40, "crit_mult": 0.90, "speed": 1.00, "perception": 1.00, "stealth": 0.70 },
    "lanceur":  { "hp": 0.75, "resource": 1.30, "damage": 1.30, "accuracy": 1.00, "range": 1.10, "crit_rate": 0.70, "crit_mult": 1.20, "speed": 1.00, "perception": 1.15, "stealth": 0.60 }
  },
  "race_profiles": {
    "humains": { "hp": 1.00, "resource": 1.00, "damage": 1.00, "accuracy": 1.00, "range": 1.00, "crit_rate": 1.00, "crit_mult": 1.00, "speed_walk": 1.00, "speed_run": 1.00, "perception": 1.00, "stealth": 1.00 },
    "nains":   { "hp": 1.20, "resource": 1.00, "damage": 1.00, "accuracy": 1.00, "range": 0.95, "crit_rate": 0.90, "crit_mult": 1.05, "speed_walk": 0.85, "speed_run": 0.80, "perception": 0.90, "stealth": 0.80 },
    "orcs":    { "hp": 1.15, "resource": 0.90, "damage": 1.20, "accuracy": 0.95, "range": 0.95, "crit_rate": 0.95, "crit_mult": 1.10, "speed_walk": 1.00, "speed_run": 1.00, "perception": 0.90, "stealth": 0.85 },
    "elfes":   { "hp": 0.90, "resource": 1.10, "damage": 0.95, "accuracy": 1.10, "range": 1.15, "crit_rate": 1.10, "crit_mult": 0.95, "speed_walk": 1.05, "speed_run": 1.15, "perception": 1.25, "stealth": 1.20 },
    "demons":  { "hp": 1.05, "resource": 1.20, "damage": 1.10, "accuracy": 1.00, "range": 1.00, "crit_rate": 1.00, "crit_mult": 1.10, "speed_walk": 1.00, "speed_run": 1.00, "perception": 1.05, "stealth": 0.90 }
  },
  "sex_profiles": {
    "tank":     { "H": { "hp": 0.95, "crit_mult": 1.03 }, "F": { "hp": 1.05, "crit_mult": 0.97, "speed_walk": 1.08, "speed_run": 1.08 } },
    "melee":    { "H": { "hp": 0.92, "damage": 1.08, "crit_mult": 1.05 }, "F": { "hp": 1.08, "damage": 0.92, "crit_mult": 0.95 } },
    "sacre":    { "H": { "hp": 0.95, "damage": 1.06, "crit_mult": 1.04 }, "F": { "hp": 1.05, "resource": 1.06, "damage": 0.94, "crit_mult": 0.96 } },
    "distance": { "H": { "resource": 1.08, "accuracy": 0.92, "range": 1.08, "crit_mult": 1.05 }, "F": { "resource": 0.92, "accuracy": 1.08, "range": 0.92, "crit_mult": 0.95 } },
    "pisteur":  { "H": { "resource": 1.06, "accuracy": 0.94, "range": 1.06, "crit_mult": 1.04 }, "F": { "resource": 0.94, "accuracy": 1.06, "range": 0.94, "stealth": 1.06, "crit_mult": 0.96 } },
    "voleur":   { "H": { "damage": 1.07, "stealth": 0.93, "crit_mult": 1.05 }, "F": { "damage": 0.93, "stealth": 1.07, "crit_mult": 0.95 } },
    "healer":   { "H": { "hp": 0.95, "damage": 1.07, "accuracy": 0.94, "crit_mult": 1.04 }, "F": { "resource": 1.06, "damage": 0.93, "accuracy": 1.06, "crit_mult": 0.96 } },
    "lanceur":  { "H": { "hp": 0.95, "resource": 0.92, "damage": 1.08, "accuracy": 0.94, "range": 1.06, "crit_mult": 1.05 }, "F": { "resource": 1.08, "damage": 0.92, "accuracy": 1.06, "range": 0.94, "crit_mult": 0.95 } }
  }
}
```

- [ ] **Step 2: Valider que c'est du JSON bien formé**

Run: `python -c "import json;json.load(open('game/data/gameplay/character_stats.json'));print('OK')"`
Expected: `OK`
> (Outil de validation locale uniquement — n'introduit pas de dépendance build.)

- [ ] **Step 3: Commit**

```bash
git add game/data/gameplay/character_stats.json
git commit -m "feat(data): character_stats.json — bases/multiplicateurs/xp (valeurs §6.3)"
```

---

## Task 4: Données — `factions.json` (taxonomie, non secrète)

**Files:**
- Create: `game/data/races/factions.json`

- [ ] **Step 1: Créer le fichier** (ids courts alignés sur §6.3 ; `selectable`)

```json
{
  "schema_version": 1,
  "factions": [
    { "id": "lumiere", "name": "Chevaliers de la Lumière", "race": "humains", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "archer", "name": "Archer", "profile": "distance", "resource": "souffle" },
      { "id": "voleur", "name": "Voleur", "profile": "voleur", "resource": "reflexes" },
      { "id": "inquisiteur_chatieur", "name": "Inquisiteur", "subclass": "Châtieur", "profile": "melee", "resource": "ferveur" },
      { "id": "inquisiteur_hospitalier", "name": "Inquisiteur", "subclass": "Hospitalier", "profile": "healer", "resource": "ferveur" }
    ] },
    { "id": "justice", "name": "Chevaliers de la Justice", "race": "humains", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "archer", "name": "Archer", "profile": "distance", "resource": "souffle" },
      { "id": "paladin", "name": "Paladin", "profile": "sacre", "resource": "ferveur" },
      { "id": "pretre_jugement", "name": "Prêtre", "subclass": "du Jugement", "profile": "lanceur", "resource": "ferveur" },
      { "id": "pretre_grace", "name": "Prêtre", "subclass": "de la Grâce", "profile": "healer", "resource": "ferveur" }
    ] },
    { "id": "lune_noire", "name": "La Lune Noire", "race": "humains", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "arbaletrier", "name": "Arbalétrier", "profile": "distance", "resource": "souffle" },
      { "id": "pretre_lune_noire", "name": "Prêtre de la Lune Noire", "profile": "lanceur", "resource": "devotion" },
      { "id": "menthats", "name": "Menthats", "profile": "lanceur", "resource": "magie" }
    ] },
    { "id": "dzorak", "name": "Dzorak", "race": "orcs", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "chaman", "name": "Chaman", "profile": "lanceur", "resource": "transe" },
      { "id": "archer", "name": "Archer", "profile": "distance", "resource": "souffle" },
      { "id": "pisteur", "name": "Pisteur", "profile": "pisteur", "resource": "instinct" }
    ] },
    { "id": "legion", "name": "Légion infernale", "race": "demons", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "demoniste", "name": "Démoniste", "profile": "lanceur", "resource": "corruption" },
      { "id": "tourmenteur", "name": "Tourmenteur", "profile": "melee", "resource": "corruption" },
      { "id": "sorcier_sang", "name": "Sorcier de sang", "profile": "lanceur", "resource": "corruption" }
    ] },
    { "id": "dragons", "name": "Chevaliers-Dragons", "race": "humains", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "archimage", "name": "Archimage", "profile": "lanceur", "resource": "flamme_draconique" },
      { "id": "dragonnier", "name": "Dragonnier", "profile": "melee", "resource": "furie_draconique" },
      { "id": "gardien_ecailles", "name": "Gardien d'écailles", "profile": "tank", "resource": "ecaille" }
    ] },
    { "id": "serpent", "name": "Maison du Serpent", "race": "humains", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "archimage", "name": "Archimage", "profile": "lanceur", "resource": "magie_base" },
      { "id": "assassin", "name": "Assassin", "profile": "voleur", "resource": "reflexes" },
      { "id": "mage", "name": "Mage", "profile": "lanceur", "resource": "magie_base" }
    ] },
    { "id": "naine", "name": "Faction Naine", "race": "nains", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "pisteur", "name": "Pisteur", "profile": "pisteur", "resource": "instinct" },
      { "id": "mage", "name": "Mage", "profile": "lanceur", "resource": "magie_base" },
      { "id": "brise_roc", "name": "Brise-roc", "profile": "tank", "resource": "endurance" }
    ] },
    { "id": "elfe", "name": "Faction Elfe", "race": "elfes", "selectable": true, "classes": [
      { "id": "guerrier", "name": "Guerrier", "profile": "melee", "resource": "endurance" },
      { "id": "archer_bois", "name": "Archer", "subclass": "Bois", "profile": "distance", "resource": "souffle" },
      { "id": "voleur_tenebreux", "name": "Voleur", "subclass": "Ténébreux", "profile": "voleur", "resource": "reflexes" },
      { "id": "mage", "name": "Mage", "profile": "lanceur", "resource": "magie_base" }
    ] },
    { "id": "empire_hynn", "name": "L'Empire de L'hynn", "race": "humains", "selectable": false, "classes": [] }
  ]
}
```

- [ ] **Step 2: Valider JSON**

Run: `python -c "import json;json.load(open('game/data/races/factions.json'));print('OK')"`
Expected: `OK`

- [ ] **Step 3: Commit**

```bash
git add game/data/races/factions.json
git commit -m "feat(data): factions.json — 9 factions jouables + empire_hynn (non sélectionnable)"
```

---

## Task 5: Codegen CMake — JSON → header embarqué

**Files:**
- Create: `cmake/EmbedJson.cmake`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Créer le script d'embarquement**

`cmake/EmbedJson.cmake` :

```cmake
# EmbedJson.cmake — convertit un fichier JSON en header C++ exposant le contenu
# comme const char* (raw string literal). Invoqué via `cmake -P` par un
# add_custom_command. Paramètres : -DINPUT, -DOUTPUT, -DSYMBOL.
# Aucun chemin absolu en dur ; tout vient des -D.
if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT OR NOT DEFINED SYMBOL)
  message(FATAL_ERROR "EmbedJson.cmake : INPUT, OUTPUT et SYMBOL requis")
endif()
file(READ "${INPUT}" CONTENT)
# Délimiteur de raw string improbable dans le JSON de jeu.
set(GEN "// AUTO-GENERATED par EmbedJson.cmake — NE PAS EDITER.\n")
string(APPEND GEN "// Source : ${INPUT}\n#pragma once\n")
string(APPEND GEN "namespace engine::server::gameplay {\n")
string(APPEND GEN "inline constexpr const char* ${SYMBOL} = R\"LCDLLN_EMBED(\n")
string(APPEND GEN "${CONTENT}")
string(APPEND GEN "\n)LCDLLN_EMBED\";\n}\n")
file(WRITE "${OUTPUT}" "${GEN}")
```

- [ ] **Step 2: Ajouter les commandes de génération dans `src/CMakeLists.txt`**

À placer **avant** la définition de `server_app` (les deux branches), au niveau racine du fichier (zone commune) :

```cmake
# ── Embarquement anti-triche : JSON game/data → headers const (générés au build).
set(LCDLLN_GEN_DIR ${CMAKE_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${LCDLLN_GEN_DIR})

set(STATS_JSON    ${CMAKE_SOURCE_DIR}/game/data/gameplay/character_stats.json)
set(STATS_HDR     ${LCDLLN_GEN_DIR}/CharacterStatsData.h)
set(FACTIONS_JSON ${CMAKE_SOURCE_DIR}/game/data/races/factions.json)
set(FACTIONS_HDR  ${LCDLLN_GEN_DIR}/FactionsData.h)

add_custom_command(
  OUTPUT  ${STATS_HDR}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${STATS_JSON} -DOUTPUT=${STATS_HDR} -DSYMBOL=kCharacterStatsJson -P ${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake
  DEPENDS ${STATS_JSON} ${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake
  COMMENT "Embedding character_stats.json -> CharacterStatsData.h")

add_custom_command(
  OUTPUT  ${FACTIONS_HDR}
  COMMAND ${CMAKE_COMMAND} -DINPUT=${FACTIONS_JSON} -DOUTPUT=${FACTIONS_HDR} -DSYMBOL=kFactionsJson -P ${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake
  DEPENDS ${FACTIONS_JSON} ${CMAKE_SOURCE_DIR}/cmake/EmbedJson.cmake
  COMMENT "Embedding factions.json -> FactionsData.h")

add_custom_target(lcdlln_gen_character_data DEPENDS ${STATS_HDR} ${FACTIONS_HDR})
```

> Les cibles qui incluent les headers générés devront : `add_dependencies(<cible> lcdlln_gen_character_data)` et `target_include_directories(<cible> PRIVATE ${LCDLLN_GEN_DIR})`. C'est fait dans les Tasks 7 et 11.

- [ ] **Step 3: Commit**

```bash
git add cmake/EmbedJson.cmake src/CMakeLists.txt
git commit -m "build(cmake): codegen JSON->header pour embarquement anti-triche shardd"
```

---

## Task 6: `Formulas.h` — nouvelle courbe XP paramétrée

**Files:**
- Modify: `src/shared/formulas/Formulas.h:10-18`
- Modify: `src/shared/formulas/FormulasTests.cpp:8-18`

- [ ] **Step 1: Mettre à jour le test (échoue avant impl)**

Remplacer `TestXpProgression` dans `src/shared/formulas/FormulasTests.cpp` par :

```cpp
	bool TestXpProgression()
	{
		// Params du design (character_stats.json) passés explicitement.
		constexpr double kBase = 6.185;
		constexpr double kFactor = 2.6;
		constexpr uint8_t kMax = 100;

		// Cap : 0 hors plage et au niveau max.
		if (XpToNextLevel(0,   kBase, kFactor, kMax) != 0) return false;
		if (XpToNextLevel(100, kBase, kFactor, kMax) != 0) return false;
		// Positivité sur 1..99.
		if (XpToNextLevel(1,  kBase, kFactor, kMax) == 0) return false;
		if (XpToNextLevel(99, kBase, kFactor, kMax) == 0) return false;
		// Monotonie stricte.
		if (!(XpToNextLevel(1, kBase, kFactor, kMax) < XpToNextLevel(2, kBase, kFactor, kMax))) return false;
		if (!(XpToNextLevel(2, kBase, kFactor, kMax) < XpToNextLevel(99, kBase, kFactor, kMax))) return false;
		// Ancrage forme : XP(10) ~= round(6.185 * 10^2.6) = round(6.185 * 398.107) = 2463.
		const uint32_t x10 = XpToNextLevel(10, kBase, kFactor, kMax);
		if (x10 < 2460u || x10 > 2466u) return false;
		LOG_INFO(Core, "[FormulasTests] xp progression OK");
		return true;
	}
```

- [ ] **Step 2: Lancer le test → échec attendu**

Run (CI build-linux) : `ctest -R formulas`
Expected: FAIL compilation (signature `XpToNextLevel` à 1 argument).

- [ ] **Step 3: Remplacer `XpToNextLevel` dans `Formulas.h`**

Remplacer les lignes 10-18 par :

```cpp
	/// XP requis pour passer du niveau \p level au level+1.
	/// Courbe du design : round(base * level^factor). Paramètres fournis par
	/// l'appelant (issus de character_stats.json embarqué côté serveur) — JAMAIS
	/// codés en dur ici. Renvoie 0 si level == 0 ou level >= levelMax (cap).
	/// \param base   coefficient multiplicateur de la courbe.
	/// \param factor exposant (2.6 dans le design).
	/// \param levelMax niveau maximum (100) ; au-delà, plus de progression.
	inline uint32_t XpToNextLevel(uint8_t level, double base, double factor, uint8_t levelMax)
	{
		if (level == 0 || level >= levelMax) return 0;
		const double xp = base * std::pow(static_cast<double>(level), factor);
		if (xp <= 0.0) return 0;
		return static_cast<uint32_t>(xp + 0.5); // round-half-up
	}
```

Ajouter l'include `<cmath>` en tête de `Formulas.h` (après `<algorithm>`) :

```cpp
#include <cmath>
```

- [ ] **Step 4: Lancer le test → succès attendu**

Run (CI) : `ctest -R formulas`
Expected: PASS `[FormulasTests] ALL OK`

- [ ] **Step 5: Commit**

```bash
git add src/shared/formulas/Formulas.h src/shared/formulas/FormulasTests.cpp
git commit -m "feat(formulas): courbe XP paramétrée base*N^2.6 cap 100 (remplace cubique cap 80)"
```

---

## Task 7: `CharacterStatsTables` + `CharacterStatsEngine`

Le cœur : parser les tables embarquées et calculer les 11 stats.

**Files:**
- Create: `src/shardd/gameplay/character/CharacterStatsTables.h`
- Create: `src/shardd/gameplay/character/CharacterStatsTables.cpp`
- Create: `src/shardd/gameplay/character/CharacterStatsEngine.h`
- Create: `src/shardd/gameplay/character/CharacterStatsEngine.cpp`
- Test: `src/shardd/gameplay/character/CharacterStatsEngineTests.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Définir les structures de tables (`CharacterStatsTables.h`)**

```cpp
#pragma once
// CharacterStatsTables : tables de stats chargées depuis le JSON embarqué
// (character_stats.json) + la taxonomie embarquée (factions.json). Construites
// une fois au boot via FromEmbedded(). Aucun accès disque (anti-triche).

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace engine::server::gameplay
{
	/// Base d'une stat interpolée linéairement lvl1 -> lvl100.
	struct StatBase { double lvl1 = 0.0; double lvl100 = 0.0; };

	/// Multiplicateurs d'un profil d'archétype (class_profiles).
	/// Ordre logique : hp,resource,damage,accuracy,range,crit_rate,crit_mult,speed,perception,stealth.
	struct ClassProfile
	{
		double hp=1, resource=1, damage=1, accuracy=1, range=1,
		       crit_rate=1, crit_mult=1, speed=1, perception=1, stealth=1;
	};

	/// Multiplicateurs d'une race (race_profiles). speed_walk/speed_run séparés.
	struct RaceProfile
	{
		double hp=1, resource=1, damage=1, accuracy=1, range=1,
		       crit_rate=1, crit_mult=1, speed_walk=1, speed_run=1, perception=1, stealth=1;
	};

	/// Multiplicateurs de sexe pour un profil (clés absentes = 1.0).
	/// On stocke par nom de stat pour gérer l'absence (get(stat,1.0)).
	struct SexMods { std::unordered_map<std::string,double> H; std::unordered_map<std::string,double> F; };

	/// Une classe dans une faction (factions.json) : mapping vers profil + ressource.
	struct ClassEntry { std::string id; std::string profile; std::string resource; };

	/// Une faction (factions.json).
	struct FactionEntry { std::string id; std::string race; bool selectable=false;
	                      std::unordered_map<std::string, ClassEntry> classesById; };

	/// Ensemble des tables, prêt pour le calcul.
	struct CharacterStatsTables
	{
		uint32_t levelMax = 100;
		double xpBase = 0.0, xpFactor = 0.0;

		std::unordered_map<std::string, StatBase> bases; // hp,resource,damage,accuracy,range,crit_rate,stamina
		double critMultBase = 1.5;
		double critRateCap = 10.0;
		double speedWalkBase = 2.0, speedRunBase = 5.0, speedSprintBase = 8.0;
		double perceptionLvl1 = 10.0, perceptionPerLevel = 0.5;

		std::unordered_map<std::string, ClassProfile> classProfiles;
		std::unordered_map<std::string, RaceProfile>  raceProfiles;
		std::unordered_map<std::string, SexMods>      sexProfiles; // clé = profil
		std::unordered_map<std::string, FactionEntry> factions;    // clé = factionId

		/// Construit les tables depuis les chaînes JSON embarquées.
		/// \param statsJson contenu de character_stats.json (kCharacterStatsJson).
		/// \param factionsJson contenu de factions.json (kFactionsJson).
		/// \return nullopt si un JSON est invalide ou incomplet.
		static std::optional<CharacterStatsTables> FromEmbedded(
			const char* statsJson, const char* factionsJson);
	};
}
```

- [ ] **Step 2: Implémenter le parsing (`CharacterStatsTables.cpp`)**

```cpp
#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include "src/shared/core/Config.h"

#include <string>

namespace engine::server::gameplay
{
	namespace
	{
		using engine::core::Config;

		ClassProfile ParseClassProfile(const Config& c, const std::string& p)
		{
			ClassProfile cp;
			cp.hp         = c.GetDouble(p + ".hp", 1.0);
			cp.resource   = c.GetDouble(p + ".resource", 1.0);
			cp.damage     = c.GetDouble(p + ".damage", 1.0);
			cp.accuracy   = c.GetDouble(p + ".accuracy", 1.0);
			cp.range      = c.GetDouble(p + ".range", 1.0);
			cp.crit_rate  = c.GetDouble(p + ".crit_rate", 1.0);
			cp.crit_mult  = c.GetDouble(p + ".crit_mult", 1.0);
			cp.speed      = c.GetDouble(p + ".speed", 1.0);
			cp.perception = c.GetDouble(p + ".perception", 1.0);
			cp.stealth    = c.GetDouble(p + ".stealth", 1.0);
			return cp;
		}

		RaceProfile ParseRaceProfile(const Config& c, const std::string& p)
		{
			RaceProfile rp;
			rp.hp         = c.GetDouble(p + ".hp", 1.0);
			rp.resource   = c.GetDouble(p + ".resource", 1.0);
			rp.damage     = c.GetDouble(p + ".damage", 1.0);
			rp.accuracy   = c.GetDouble(p + ".accuracy", 1.0);
			rp.range      = c.GetDouble(p + ".range", 1.0);
			rp.crit_rate  = c.GetDouble(p + ".crit_rate", 1.0);
			rp.crit_mult  = c.GetDouble(p + ".crit_mult", 1.0);
			rp.speed_walk = c.GetDouble(p + ".speed_walk", 1.0);
			rp.speed_run  = c.GetDouble(p + ".speed_run", 1.0);
			rp.perception = c.GetDouble(p + ".perception", 1.0);
			rp.stealth    = c.GetDouble(p + ".stealth", 1.0);
			return rp;
		}

		// Lit les sous-clés connues d'un profil de sexe (clé absente = non insérée => 1.0 au calcul).
		void ParseSexSide(const Config& c, const std::string& p, std::unordered_map<std::string,double>& out)
		{
			static const char* kStats[] = { "hp","resource","damage","accuracy","range",
			                                 "crit_mult","speed_walk","speed_run","perception","stealth" };
			for (const char* s : kStats)
			{
				const std::string key = p + "." + s;
				if (c.Has(key)) out[s] = c.GetDouble(key, 1.0);
			}
		}
	}

	std::optional<CharacterStatsTables> CharacterStatsTables::FromEmbedded(
		const char* statsJson, const char* factionsJson)
	{
		if (!statsJson || !factionsJson) return std::nullopt;

		Config s;
		if (!s.LoadFromString(statsJson)) return std::nullopt;
		Config f;
		if (!f.LoadFromString(factionsJson)) return std::nullopt;

		CharacterStatsTables t;
		t.levelMax = static_cast<uint32_t>(s.GetInt("level_max", 100));
		t.xpBase   = s.GetDouble("xp.base", 0.0);
		t.xpFactor = s.GetDouble("xp.factor", 0.0);
		if (t.levelMax < 2 || t.xpBase <= 0.0 || t.xpFactor <= 0.0) return std::nullopt;

		auto base = [&](const char* name) {
			StatBase b; b.lvl1 = s.GetDouble(std::string("bases.") + name + ".lvl1", 0.0);
			b.lvl100 = s.GetDouble(std::string("bases.") + name + ".lvl100", 0.0); return b; };
		t.bases["hp"]        = base("hp");
		t.bases["resource"]  = base("resource");
		t.bases["damage"]    = base("damage");
		t.bases["accuracy"]  = base("accuracy");
		t.bases["range"]     = base("range");
		t.bases["crit_rate"] = base("crit_rate");
		t.bases["stamina"]   = base("stamina");
		t.critMultBase     = s.GetDouble("bases.crit_mult.base", 1.5);
		t.critRateCap      = s.GetDouble("bases.crit_rate.cap", 10.0);
		t.speedWalkBase    = s.GetDouble("bases.speed_walk", 2.0);
		t.speedRunBase     = s.GetDouble("bases.speed_run", 5.0);
		t.speedSprintBase  = s.GetDouble("bases.speed_sprint", 8.0);
		t.perceptionLvl1   = s.GetDouble("bases.perception_lvl1", 10.0);
		t.perceptionPerLevel = s.GetDouble("bases.perception_per_level", 0.5);

		for (const char* p : { "tank","melee","sacre","distance","pisteur","voleur","healer","lanceur" })
			t.classProfiles[p] = ParseClassProfile(s, std::string("class_profiles.") + p);
		for (const char* r : { "humains","nains","orcs","elfes","demons" })
			t.raceProfiles[r] = ParseRaceProfile(s, std::string("race_profiles.") + r);
		for (const char* p : { "tank","melee","sacre","distance","pisteur","voleur","healer","lanceur" })
		{
			SexMods sm;
			ParseSexSide(s, std::string("sex_profiles.") + p + ".H", sm.H);
			ParseSexSide(s, std::string("sex_profiles.") + p + ".F", sm.F);
			t.sexProfiles[p] = std::move(sm);
		}

		// Factions (factions.json).
		size_t i = 0;
		while (f.Has("factions[" + std::to_string(i) + "].id"))
		{
			const std::string fp = "factions[" + std::to_string(i) + "]";
			FactionEntry fe;
			fe.id         = f.GetString(fp + ".id", "");
			fe.race       = f.GetString(fp + ".race", "");
			fe.selectable = f.GetBool(fp + ".selectable", false);
			size_t j = 0;
			while (f.Has(fp + ".classes[" + std::to_string(j) + "].id"))
			{
				const std::string cp = fp + ".classes[" + std::to_string(j) + "]";
				ClassEntry ce;
				ce.id       = f.GetString(cp + ".id", "");
				ce.profile  = f.GetString(cp + ".profile", "");
				ce.resource = f.GetString(cp + ".resource", "");
				if (!ce.id.empty()) fe.classesById[ce.id] = std::move(ce);
				++j;
			}
			if (!fe.id.empty()) t.factions[fe.id] = std::move(fe);
			++i;
		}
		if (t.factions.empty()) return std::nullopt;
		return t;
	}
}
```

- [ ] **Step 3: Définir le moteur (`CharacterStatsEngine.h`)**

```cpp
#pragma once
// CharacterStatsEngine : calcul déterministe des 11 stats à partir des tables
// embarquées + (level, factionId, classId, gender). Pur, compilé shardd-only.

#include "src/shardd/gameplay/character/CharacterStatsTables.h"

#include <cstdint>
#include <optional>
#include <string>

namespace engine::server::gameplay
{
	/// Sexe du personnage.
	enum class Sex : uint8_t { Male, Female };

	/// Résultat : les 11 stats dérivées (valeurs finales, jamais les multiplicateurs).
	struct DerivedStats
	{
		uint32_t hp = 0;            ///< PV max.
		uint32_t resource = 0;      ///< ressource secondaire max.
		uint32_t damage = 0;
		float    accuracy = 0.0f;   ///< précision %.
		float    range = 0.0f;      ///< portée m (0 si mêlée pure).
		float    critRate = 0.0f;   ///< taux de crit % (cap 10).
		float    critMult = 0.0f;   ///< multiplicateur de crit ×.
		float    speedWalk = 0.0f;
		float    speedRun = 0.0f;
		float    speedSprint = 0.0f;
		uint32_t stamina = 0;       ///< endurance max.
		float    perception = 0.0f; ///< m.
		float    stealth = 0.0f;    ///< discrétion m (bas = discret).
		std::string resourceKey;    ///< clé de ressource secondaire (ex. "ferveur").
	};

	/// Calcule les stats pour un personnage. Renvoie nullopt si la faction/classe
	/// est inconnue ou si le profil/la race référencés n'existent pas dans les tables.
	std::optional<DerivedStats> ComputeStats(const CharacterStatsTables& t,
	                                          const std::string& factionId,
	                                          const std::string& classId,
	                                          Sex sex, uint32_t level);
}
```

- [ ] **Step 4: Implémenter le moteur (`CharacterStatsEngine.cpp`)**

```cpp
#include "src/shardd/gameplay/character/CharacterStatsEngine.h"

#include <algorithm>
#include <cmath>

namespace engine::server::gameplay
{
	namespace
	{
		// Interpolation linéaire lvl1 -> lvl100 (clamp level dans [1, levelMax]).
		double BaseAt(const StatBase& b, uint32_t level, uint32_t levelMax)
		{
			const uint32_t N = std::clamp<uint32_t>(level, 1u, levelMax);
			if (levelMax <= 1) return b.lvl1;
			return b.lvl1 + (static_cast<double>(N) - 1.0) * (b.lvl100 - b.lvl1)
			              / (static_cast<double>(levelMax) - 1.0);
		}

		double SexGet(const std::unordered_map<std::string,double>& m, const char* stat)
		{
			auto it = m.find(stat);
			return it == m.end() ? 1.0 : it->second;
		}

		uint32_t RoundU(double v) { return v <= 0.0 ? 0u : static_cast<uint32_t>(v + 0.5); }
	}

	std::optional<DerivedStats> ComputeStats(const CharacterStatsTables& t,
	                                          const std::string& factionId,
	                                          const std::string& classId,
	                                          Sex sex, uint32_t level)
	{
		auto fit = t.factions.find(factionId);
		if (fit == t.factions.end()) return std::nullopt;
		auto cit = fit->second.classesById.find(classId);
		if (cit == fit->second.classesById.end()) return std::nullopt;

		const std::string& profile = cit->second.profile;
		const std::string& race    = fit->second.race;

		auto pit = t.classProfiles.find(profile);
		auto rit = t.raceProfiles.find(race);
		auto sit = t.sexProfiles.find(profile);
		if (pit == t.classProfiles.end() || rit == t.raceProfiles.end()) return std::nullopt;

		const ClassProfile& cp = pit->second;
		const RaceProfile&  rp = rit->second;
		const auto& sx = (sit != t.sexProfiles.end())
			? (sex == Sex::Male ? sit->second.H : sit->second.F)
			: std::unordered_map<std::string,double>{};

		const uint32_t lvlMax = t.levelMax;
		auto B = [&](const char* name) -> double {
			auto it = t.bases.find(name); return it == t.bases.end() ? 0.0 : BaseAt(it->second, level, lvlMax); };

		DerivedStats d;
		d.resourceKey = cit->second.resource;

		d.hp       = RoundU(B("hp")       * cp.hp       * rp.hp       * SexGet(sx, "hp"));
		d.resource = RoundU(B("resource") * cp.resource * rp.resource * SexGet(sx, "resource"));
		d.damage   = RoundU(B("damage")   * cp.damage   * rp.damage   * SexGet(sx, "damage"));
		d.stamina  = RoundU(B("stamina"));

		// Mêlée pure : range profil == 0 -> portée ET précision nulles.
		if (cp.range == 0.0)
		{
			d.range = 0.0f;
			d.accuracy = 0.0f;
		}
		else
		{
			d.range    = static_cast<float>(B("range")    * cp.range    * rp.range    * SexGet(sx, "range"));
			d.accuracy = static_cast<float>(B("accuracy") * cp.accuracy * rp.accuracy * SexGet(sx, "accuracy"));
		}

		// Crit rate : plafond garanti par la formule (jamais > cap).
		const double critRaw = B("crit_rate") * cp.crit_rate * rp.crit_rate; // crit neutre au sexe
		d.critRate = static_cast<float>(std::min(t.critRateCap, critRaw));

		d.critMult = static_cast<float>(t.critMultBase * cp.crit_mult * rp.crit_mult * SexGet(sx, "crit_mult"));

		// Vitesses : marche = walk ; course/sprint = base run (cf. spec §5).
		d.speedWalk   = static_cast<float>(t.speedWalkBase  * cp.speed * rp.speed_walk * SexGet(sx, "speed_walk"));
		d.speedRun    = static_cast<float>(t.speedRunBase   * cp.speed * rp.speed_run  * SexGet(sx, "speed_run"));
		d.speedSprint = static_cast<float>(t.speedSprintBase* cp.speed * rp.speed_run  * SexGet(sx, "speed_run"));

		const double perceptionBase = t.perceptionLvl1 + t.perceptionPerLevel * (static_cast<double>(std::max(1u, level)) - 1.0);
		d.perception = static_cast<float>(perceptionBase * cp.perception * rp.perception * SexGet(sx, "perception"));

		// Discrétion = (perception_base / 2) / (stealth classe*race*sexe). Bas = discret.
		const double stealthDiv = cp.stealth * rp.stealth * SexGet(sx, "stealth");
		d.stealth = static_cast<float>(stealthDiv > 0.0 ? (perceptionBase / 2.0) / stealthDiv : 0.0);

		return d;
	}
}
```

- [ ] **Step 5: Écrire les tests (`CharacterStatsEngineTests.cpp`)**

```cpp
// Tests du moteur de stats : round-trip JSON embarqué -> tables -> calcul,
// ancres exactes (niveau 1 et 60), invariants (cap crit, mêlée pure).
#include "src/shardd/gameplay/character/CharacterStatsEngine.h"
#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include "src/shared/core/Log.h"

#include "CharacterStatsData.h"  // kCharacterStatsJson (généré)
#include "FactionsData.h"        // kFactionsJson (généré)

#include <cmath>

namespace
{
	using namespace engine::server::gameplay;

	bool nearF(float a, double b, double tol = 0.6) { return std::fabs(static_cast<double>(a) - b) <= tol; }

	bool TestRoundTripAndAnchorsLvl1()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) return false;

		// Voleur Elfe F niv.1 (faction elfe -> race elfes, classe voleur_tenebreux -> profil voleur).
		auto d = ComputeStats(*t, "elfe", "voleur_tenebreux", Sex::Female, 1);
		if (!d) return false;
		// hp = base(1)=100 * voleur.hp 0.90 * elfes.hp 0.90 * sex(absent)=1 = 81
		if (d->hp != 81u) return false;
		// damage = 10 * 1.25 * 0.95 * sex voleur F damage 0.93 = 11.04 -> 11
		if (d->damage != 11u) return false;
		// crit_rate = 2 * voleur 1.00 * elfes 1.10 = 2.2 (neutre sexe), < cap 10
		if (!nearF(d->critRate, 2.2)) return false;
		if (d->resourceKey != "reflexes") return false;
		return true;
	}

	bool TestAnchorLvl60_GuerrierNainH()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) return false;
		// Guerrier Nain H niv.60 (faction naine -> nains, classe guerrier -> melee).
		auto d = ComputeStats(*t, "naine", "guerrier", Sex::Male, 60);
		if (!d) return false;
		// base hp(60) = 100 + 59*(3900)/99 = 2424.2424
		// hp = 2424.2424 * melee.hp 1.15 * nains.hp 1.20 * sex melee H hp 0.92 ~= 3078
		if (d->hp < 3076u || d->hp > 3080u) return false;
		// melee : mêlée pure -> range 0 ET accuracy 0
		if (d->range != 0.0f) return false;
		if (d->accuracy != 0.0f) return false;
		return true;
	}

	bool TestCritCapAndUnknownAndLanceurLvl100()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) return false;

		// Lanceur Démon F niv.100 (faction legion -> demons, classe demoniste -> lanceur).
		auto d = ComputeStats(*t, "legion", "demoniste", Sex::Female, 100);
		if (!d) return false;
		// crit jamais > cap 10
		if (d->critRate > 10.0f) return false;
		// lanceur a une portée (range profil 1.10 != 0) -> accuracy > 0
		if (!(d->range > 0.0f) || !(d->accuracy > 0.0f)) return false;
		if (d->resourceKey != "corruption") return false;

		// Faction/classe inconnue -> nullopt
		if (ComputeStats(*t, "inconnue", "guerrier", Sex::Male, 1)) return false;
		if (ComputeStats(*t, "naine", "inconnue", Sex::Male, 1)) return false;
		return true;
	}

	bool TestDeterminism()
	{
		auto t = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!t) return false;
		auto a = ComputeStats(*t, "dzorak", "pisteur", Sex::Male, 42);
		auto b = ComputeStats(*t, "dzorak", "pisteur", Sex::Male, 42);
		if (!a || !b) return false;
		return a->hp == b->hp && a->damage == b->damage && nearF(a->stealth, b->stealth, 0.0001);
	}
}

int main()
{
	engine::core::LogSettings s; s.level = engine::core::LogLevel::Info; s.console = true;
	engine::core::Log::Init(s);
	const bool ok = TestRoundTripAndAnchorsLvl1()
	             && TestAnchorLvl60_GuerrierNainH()
	             && TestCritCapAndUnknownAndLanceurLvl100()
	             && TestDeterminism();
	if (ok) LOG_INFO(Core, "[CharacterStatsEngineTests] ALL OK");
	else    LOG_ERROR(Core, "[CharacterStatsEngineTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 6: Enregistrer sources + test dans `src/CMakeLists.txt`**

Ajouter les deux `.cpp` aux **deux** listes sources de `server_app` (branche MSVC/Windows ~ligne 18-66 et branche Linux ~ligne 75-227), près des autres `gameplay/character/*` :

```cmake
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
```

Puis brancher la dépendance codegen + include sur `server_app` (dans chaque branche, après sa définition) :

```cmake
  add_dependencies(server_app lcdlln_gen_character_data)
  target_include_directories(server_app PRIVATE ${LCDLLN_GEN_DIR})
```

Enfin, le test (zone Linux des tests) :

```cmake
  add_executable(character_stats_engine_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngineTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp)
  add_dependencies(character_stats_engine_tests lcdlln_gen_character_data)
  target_include_directories(character_stats_engine_tests PRIVATE ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
  target_link_libraries(character_stats_engine_tests PRIVATE engine_core spdlog::spdlog)
  target_compile_options(character_stats_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME character_stats_engine_tests COMMAND character_stats_engine_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 7: Commit**

```bash
git add src/shardd/gameplay/character/CharacterStatsTables.h src/shardd/gameplay/character/CharacterStatsTables.cpp \
        src/shardd/gameplay/character/CharacterStatsEngine.h src/shardd/gameplay/character/CharacterStatsEngine.cpp \
        src/shardd/gameplay/character/CharacterStatsEngineTests.cpp src/CMakeLists.txt
git commit -m "feat(shardd): CharacterStatsEngine — 11 stats déterministes depuis tables embarquées"
```

> CI : vérifier `build-linux` → `ctest -R character_stats_engine_tests` vert. Si un anchor échoue, comparer l'arithmétique (ordre : base(N) double → ×profil ×race ×sexe → round une seule fois).

---

## Task 8: Extension `Unit` — nouveaux `UpdateField`

**Files:**
- Modify: `src/shardd/entities/UpdateFieldIndices.h:47-59`
- Modify: `src/shardd/entities/Unit.h`
- Modify: `src/shardd/entities/UnitTests.cpp`

- [ ] **Step 1: Écrire le test des nouveaux champs (échoue)**

Ajouter dans `src/shardd/entities/UnitTests.cpp` une fonction de test (et l'appeler depuis `main`) :

```cpp
	bool TestNewStatFields()
	{
		using namespace engine::server::entities;
		Unit u(ObjectGuid{ 1 });
		u.SetDamage(123u);
		u.SetCritRate(7.5f);
		u.SetCritMult(1.8f);
		u.SetSpeedWalk(2.0f); u.SetSpeedRun(5.0f); u.SetSpeedSprint(8.0f);
		u.SetStamina(500u); u.SetMaxStamina(800u);
		u.SetPerception(12.5f); u.SetStealth(9.0f);
		u.SetAccuracy(88.0f); u.SetRange(30.0f);
		u.SetSecondaryResource(40u); u.SetMaxSecondaryResource(100u);

		if (u.GetDamage() != 123u) return false;
		if (u.GetCritRate() < 7.49f || u.GetCritRate() > 7.51f) return false;
		if (u.GetMaxStamina() != 800u) return false;
		if (u.GetSecondaryResource() != 40u) return false;
		// Le mask doit refléter au moins un champ modifié.
		if (u.Mask().Empty()) return false;
		return true;
	}
```

> Vérifier dans `UnitTests.cpp` comment `main` agrège les tests et y ajouter `&& TestNewStatFields()`. Vérifier l'accès au mask (méthode `Mask()` héritée d'`Object` — confirmer son nom exact dans `Object.h`).

- [ ] **Step 2: Ajouter les indices (appended) dans `UpdateFieldIndices.h`**

Remplacer l'enum `UnitFieldIdx` (lignes 47-59) par :

```cpp
	/// Indices pour Unit (extends WorldObject). Demarre apres WorldObject end.
	/// IMPORTANT : ne JAMAIS reordonner/reassigner ; ajouter en fin avant kUnitFieldEnd.
	enum UnitFieldIdx : size_t
	{
		kUnitFieldHealth      = kWorldObjectFieldEnd,      // 11
		kUnitFieldMaxHealth   = kWorldObjectFieldEnd + 1,  // 12
		kUnitFieldMana        = kWorldObjectFieldEnd + 2,  // 13
		kUnitFieldMaxMana     = kWorldObjectFieldEnd + 3,  // 14
		kUnitFieldLevel       = kWorldObjectFieldEnd + 4,  // 15
		kUnitFieldFaction     = kWorldObjectFieldEnd + 5,  // 16
		// --- Stats étendues (Système de Personnages) ---
		kUnitFieldDamage              = kWorldObjectFieldEnd + 6,  // 17
		kUnitFieldAccuracy            = kWorldObjectFieldEnd + 7,  // 18
		kUnitFieldRange               = kWorldObjectFieldEnd + 8,  // 19
		kUnitFieldCritRate            = kWorldObjectFieldEnd + 9,  // 20
		kUnitFieldCritMult            = kWorldObjectFieldEnd + 10, // 21
		kUnitFieldSpeedWalk           = kWorldObjectFieldEnd + 11, // 22
		kUnitFieldSpeedRun            = kWorldObjectFieldEnd + 12, // 23
		kUnitFieldSpeedSprint         = kWorldObjectFieldEnd + 13, // 24
		kUnitFieldStamina             = kWorldObjectFieldEnd + 14, // 25
		kUnitFieldMaxStamina          = kWorldObjectFieldEnd + 15, // 26
		kUnitFieldPerception          = kWorldObjectFieldEnd + 16, // 27
		kUnitFieldStealth             = kWorldObjectFieldEnd + 17, // 28
		kUnitFieldSecondaryResource   = kWorldObjectFieldEnd + 18, // 29
		kUnitFieldMaxSecondaryResource= kWorldObjectFieldEnd + 19, // 30

		kUnitFieldEnd         = kWorldObjectFieldEnd + 20   // 31
	};
```

> `kPlayerFieldIdx` et `kCreatureFieldIdx` démarrent à `kUnitFieldEnd` : ils se décalent automatiquement (les valeurs Player/Creature passent de 17.. à 31..). C'est attendu (wire format Player/Creature non encore gelé en prod ; aucun client live ne consomme ce path — cf. R1).

- [ ] **Step 3: Ajouter les `UpdateField` + accesseurs dans `Unit.h`**

Dans le constructeur `Unit(...)`, après l'init de `m_faction` (ligne 34), ajouter aux initialisations :

```cpp
			, m_damage(kUnitFieldDamage, &Mask())
			, m_accuracy(kUnitFieldAccuracy, &Mask())
			, m_range(kUnitFieldRange, &Mask())
			, m_critRate(kUnitFieldCritRate, &Mask())
			, m_critMult(kUnitFieldCritMult, &Mask())
			, m_speedWalk(kUnitFieldSpeedWalk, &Mask())
			, m_speedRun(kUnitFieldSpeedRun, &Mask())
			, m_speedSprint(kUnitFieldSpeedSprint, &Mask())
			, m_stamina(kUnitFieldStamina, &Mask())
			, m_maxStamina(kUnitFieldMaxStamina, &Mask())
			, m_perception(kUnitFieldPerception, &Mask())
			, m_stealth(kUnitFieldStealth, &Mask())
			, m_secondaryResource(kUnitFieldSecondaryResource, &Mask())
			, m_maxSecondaryResource(kUnitFieldMaxSecondaryResource, &Mask())
```

Avant le `private:` (après `IsAlive()`), ajouter les accesseurs :

```cpp
		void SetDamage(uint32_t v) { m_damage.Set(v); }
		uint32_t GetDamage() const noexcept { return m_damage.Get(); }
		void SetAccuracy(float v) { m_accuracy.Set(v); }
		float GetAccuracy() const noexcept { return m_accuracy.Get(); }
		void SetRange(float v) { m_range.Set(v); }
		float GetRange() const noexcept { return m_range.Get(); }
		void SetCritRate(float v) { m_critRate.Set(v); }
		float GetCritRate() const noexcept { return m_critRate.Get(); }
		void SetCritMult(float v) { m_critMult.Set(v); }
		float GetCritMult() const noexcept { return m_critMult.Get(); }
		void SetSpeedWalk(float v) { m_speedWalk.Set(v); }
		float GetSpeedWalk() const noexcept { return m_speedWalk.Get(); }
		void SetSpeedRun(float v) { m_speedRun.Set(v); }
		float GetSpeedRun() const noexcept { return m_speedRun.Get(); }
		void SetSpeedSprint(float v) { m_speedSprint.Set(v); }
		float GetSpeedSprint() const noexcept { return m_speedSprint.Get(); }
		void SetStamina(uint32_t v) { m_stamina.Set(v); }
		uint32_t GetStamina() const noexcept { return m_stamina.Get(); }
		void SetMaxStamina(uint32_t v) { m_maxStamina.Set(v); }
		uint32_t GetMaxStamina() const noexcept { return m_maxStamina.Get(); }
		void SetPerception(float v) { m_perception.Set(v); }
		float GetPerception() const noexcept { return m_perception.Get(); }
		void SetStealth(float v) { m_stealth.Set(v); }
		float GetStealth() const noexcept { return m_stealth.Get(); }
		void SetSecondaryResource(uint32_t v) { m_secondaryResource.Set(v); }
		uint32_t GetSecondaryResource() const noexcept { return m_secondaryResource.Get(); }
		void SetMaxSecondaryResource(uint32_t v) { m_maxSecondaryResource.Set(v); }
		uint32_t GetMaxSecondaryResource() const noexcept { return m_maxSecondaryResource.Get(); }
```

Dans la section `private:`, après `m_faction` (ligne 77), ajouter les membres :

```cpp
		UpdateField<uint32_t> m_damage;
		UpdateField<float>    m_accuracy;
		UpdateField<float>    m_range;
		UpdateField<float>    m_critRate;
		UpdateField<float>    m_critMult;
		UpdateField<float>    m_speedWalk;
		UpdateField<float>    m_speedRun;
		UpdateField<float>    m_speedSprint;
		UpdateField<uint32_t> m_stamina;
		UpdateField<uint32_t> m_maxStamina;
		UpdateField<float>    m_perception;
		UpdateField<float>    m_stealth;
		UpdateField<uint32_t> m_secondaryResource;
		UpdateField<uint32_t> m_maxSecondaryResource;
```

> Vérifier que `Mask()` est accessible dans le ctor (déjà utilisé pour `m_health`). Vérifier que `UpdateField<float>` compile (template générique : OK, `operator!=` sur float existe).

- [ ] **Step 4: Lancer les tests Unit → succès**

Run (CI) : `ctest -R unit`
Expected: PASS (UnitTests inclut `TestNewStatFields`).

- [ ] **Step 5: Commit**

```bash
git add src/shardd/entities/UpdateFieldIndices.h src/shardd/entities/Unit.h src/shardd/entities/UnitTests.cpp
git commit -m "feat(entities): Unit — 14 UpdateField de stats étendues (réplication delta)"
```

---

## Task 9: `Player::ApplyDerivedStats`

Relie le moteur (Task 7) aux `UpdateField` (Task 8). Pas de câblage au runtime live (R1 différé) — méthode testée en isolation.

**Files:**
- Modify: `src/shardd/entities/Player.h`
- Modify: `src/shardd/entities/Player.cpp`
- Modify: `src/shardd/entities/PlayerTests.cpp`
- Modify: `src/CMakeLists.txt` (PlayerTests doit linker le moteur + codegen)

- [ ] **Step 1: Écrire le test (échoue)**

Ajouter dans `src/shardd/entities/PlayerTests.cpp` (et appeler depuis `main`) :

```cpp
	bool TestApplyDerivedStats()
	{
		using namespace engine::server::entities;
		using namespace engine::server::gameplay;

		auto tables = CharacterStatsTables::FromEmbedded(kCharacterStatsJson, kFactionsJson);
		if (!tables) return false;

		Player p(ObjectGuid{ 7 }, /*account*/ 1, /*char*/ 2, "Tester");
		p.SetLevel(1);
		// Voleur Elfe F niv.1 -> hp attendu 81 (cf. CharacterStatsEngineTests).
		if (!p.ApplyDerivedStats(*tables, "elfe", "voleur_tenebreux", Sex::Female)) return false;
		if (p.GetMaxHealth() != 81u) return false;
		if (p.GetHealth() != 81u) return false; // plein à l'application
		if (p.GetSecondaryResource() != p.GetMaxSecondaryResource()) return false;
		return true;
	}
```

Ajouter en tête de `PlayerTests.cpp` les includes :

```cpp
#include "src/shardd/gameplay/character/CharacterStatsEngine.h"
#include "src/shardd/gameplay/character/CharacterStatsTables.h"
#include "CharacterStatsData.h"
#include "FactionsData.h"
```

- [ ] **Step 2: Déclarer la méthode dans `Player.h`**

Ajouter l'include en tête (après les includes existants) :

```cpp
#include "src/shardd/gameplay/character/CharacterStatsEngine.h"
```

Déclarer (dans la section public, après `SetXp`/`GetXp`) :

```cpp
		/// Recalcule et applique les 11 stats dérivées pour ce joueur depuis les
		/// tables embarquées + (faction, classe, sexe, niveau courant). Remplit les
		/// UpdateField (MarkDirty implicite via Set). HP et ressource sont mis au
		/// plein. À appeler après chargement DB et à chaque level-up.
		/// \return false si (faction, classe) est inconnue dans les tables.
		bool ApplyDerivedStats(const engine::server::gameplay::CharacterStatsTables& tables,
		                       const std::string& factionId,
		                       const std::string& classId,
		                       engine::server::gameplay::Sex sex);
```

- [ ] **Step 3: Implémenter dans `Player.cpp`**

Remplacer le corps de `Player.cpp` par :

```cpp
// Player : translation unit.
#include "src/shardd/entities/Player.h"

namespace engine::server::entities
{
	bool Player::ApplyDerivedStats(const engine::server::gameplay::CharacterStatsTables& tables,
	                               const std::string& factionId,
	                               const std::string& classId,
	                               engine::server::gameplay::Sex sex)
	{
		auto d = engine::server::gameplay::ComputeStats(tables, factionId, classId, sex, GetLevel());
		if (!d) return false;

		SetMaxHealth(d->hp);
		SetHealth(d->hp);                    // plein à l'application
		SetMaxSecondaryResource(d->resource);
		SetSecondaryResource(d->resource);
		SetDamage(d->damage);
		SetAccuracy(d->accuracy);
		SetRange(d->range);
		SetCritRate(d->critRate);
		SetCritMult(d->critMult);
		SetSpeedWalk(d->speedWalk);
		SetSpeedRun(d->speedRun);
		SetSpeedSprint(d->speedSprint);
		SetMaxStamina(d->stamina);
		SetStamina(d->stamina);
		SetPerception(d->perception);
		SetStealth(d->stealth);
		return true;
	}
}
```

> Note : `SetHealth` clampe à `maxHealth` — appeler `SetMaxHealth` **avant** `SetHealth` (déjà l'ordre ci-dessus).

- [ ] **Step 4: CMake — lier le moteur au binaire + au test Player**

Vérifier que `CharacterStatsEngine.cpp`/`CharacterStatsTables.cpp` sont déjà dans `server_app` (Task 7). Pour `player_tests` (chercher son `add_executable` ; sinon le créer sur le modèle des autres tests entités) :

```cmake
  add_executable(player_tests
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/PlayerTests.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/entities/Player.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/shardd/gameplay/character/CharacterStatsTables.cpp)
  add_dependencies(player_tests lcdlln_gen_character_data)
  target_include_directories(player_tests PRIVATE ${CMAKE_SOURCE_DIR} ${LCDLLN_GEN_DIR})
  target_link_libraries(player_tests PRIVATE engine_core spdlog::spdlog)
  target_compile_options(player_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME player_tests COMMAND player_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

> Si un `player_tests` existe déjà, n'ajouter QUE les nouvelles sources/`add_dependencies`/include — ne pas dupliquer la cible.

- [ ] **Step 5: Lancer → succès**

Run (CI) : `ctest -R player`
Expected: PASS (`TestApplyDerivedStats`).

- [ ] **Step 6: Commit**

```bash
git add src/shardd/entities/Player.h src/shardd/entities/Player.cpp src/shardd/entities/PlayerTests.cpp src/CMakeLists.txt
git commit -m "feat(entities): Player::ApplyDerivedStats — remplit les UpdateField depuis le moteur"
```

---

## Task 10: Migration DB `0072_factions_v2.sql`

Aligne la table `factions` (migration 0040) sur les ids courts + backfill `characters.faction_str`. Idempotente, dans **les deux** arbres.

**Files:**
- Create: `sql/migrations/0072_factions_v2.sql`
- Create: `deploy/docker/sql/migrations/0072_factions_v2.sql` (contenu identique)

- [ ] **Step 1: Écrire la migration**

```sql
-- Migration 0072 — Factions v2 : alignement sur les ids courts du design
-- (Système de Personnages). Renomme les slugs longs de 0040, ajoute les
-- nouvelles factions, repasse Chevaliers-Dragons en race humains, et
-- backfill characters.faction_str. Idempotent.

SET NAMES utf8mb4;
SET FOREIGN_KEY_CHECKS = 0;
START TRANSACTION;

-- 1) Renommer les slugs existants vers les ids courts + corriger race_lock.
UPDATE factions SET name='lumiere'    WHERE name='chevaliers_lumiere';
UPDATE factions SET name='justice'    WHERE name='chevaliers_justice';
UPDATE factions SET name='legion', display_name='Légion infernale' WHERE name='demons';
UPDATE factions SET name='dragons', race_lock='humains' WHERE name='chevaliers_dragons';
-- lune_noire, dzorak, empire_hynn : ids déjà alignés.

-- 2) Ajouter les nouvelles factions (INSERT IGNORE = idempotent sur uq_factions_name).
INSERT IGNORE INTO factions (name, display_name, race_lock, parent_faction_id, description) VALUES
  ('serpent', 'Maison du Serpent', 'humains', NULL, 'Maison humaine versée dans la magie et l''ombre.'),
  ('naine',   'Faction Naine',     'nains',   NULL, 'Peuple nain : guerriers tenaces et artisans.'),
  ('elfe',    'Faction Elfe',      'elfes',   NULL, 'Peuple elfe : agilité, magie et discrétion.');

-- 3) empire_hynn : conservée, NON sélectionnable. Ajouter la colonne `selectable`
--    si absente (défaut 1), puis marquer empire_hynn à 0.
SET @col_exists := (SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS
  WHERE table_schema = DATABASE() AND table_name='factions' AND column_name='selectable');
SET @stmt := IF(@col_exists = 0,
  'ALTER TABLE factions ADD COLUMN selectable TINYINT(1) NOT NULL DEFAULT 1 COMMENT ''0 = présente mais non sélectionnable à la création''',
  'SELECT ''column factions.selectable already exists, skipping''');
PREPARE a FROM @stmt; EXECUTE a; DEALLOCATE PREPARE a;
UPDATE factions SET selectable = 0 WHERE name = 'empire_hynn';

-- 4) Backfill characters.faction_str pour les slugs renommés (persos existants).
UPDATE characters SET faction_str='lumiere' WHERE faction_str='chevaliers_lumiere';
UPDATE characters SET faction_str='justice' WHERE faction_str='chevaliers_justice';
UPDATE characters SET faction_str='legion'  WHERE faction_str='demons';
UPDATE characters SET faction_str='dragons' WHERE faction_str='chevaliers_dragons';

COMMIT;
SET FOREIGN_KEY_CHECKS = 1;
```

- [ ] **Step 2: Dupliquer à l'identique dans l'arbre docker**

```bash
cp sql/migrations/0072_factions_v2.sql deploy/docker/sql/migrations/0072_factions_v2.sql
```

- [ ] **Step 3: Vérifier la cohérence des deux fichiers**

Run: `diff sql/migrations/0072_factions_v2.sql deploy/docker/sql/migrations/0072_factions_v2.sql`
Expected: (aucune sortie — fichiers identiques)

- [ ] **Step 4: Commit**

```bash
git add sql/migrations/0072_factions_v2.sql deploy/docker/sql/migrations/0072_factions_v2.sql
git commit -m "feat(db): migration 0072 — factions ids courts + selectable + backfill"
```

---

## Task 11: Vérification finale + push PR1

**Files:** (aucun)

- [ ] **Step 1: Vérifier qu'aucun multiplicateur n'a fui dans une cible client**

Run: `grep -rn "character_stats.json\|kCharacterStatsJson" src/client src/world_editor`
Expected: (aucune sortie — le client ne référence jamais les multiplicateurs)

- [ ] **Step 2: Vérifier que les deux branches `server_app` listent bien les nouvelles sources**

Run: `grep -n "CharacterStatsEngine.cpp\|CharacterStatsTables.cpp" src/CMakeLists.txt`
Expected: au moins 2 occurrences de chaque (branche Windows + branche Linux ; +tests).

- [ ] **Step 3: Pousser et ouvrir la PR**

```bash
git push -u origin feat/character-system-pr1-server
gh pr create --base main --title "feat: Système de Personnages PR1 (serveur) — moteur de stats + XP + réplication" \
  --body "Voir docs/superpowers/specs/2026-06-08-character-system-factions-races-classes-stats-design.md (PR1).

Inclut : factions.json + character_stats.json (embarqués shardd), migration 0072, CharacterStatsEngine (11 stats déterministes), courbe XP base*N^2.6 cap 100, extension Unit/Player UpdateField.

R1 différé : câblage des stats au chemin de réplication live (snapshot ECS) hors PR1.

**Déploiement** : ⚠️ redéploiement serveur requis — migration DB 0072 + binaire shardd. PR2 (wire+UI client) suivra en lock-step.

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

- [ ] **Step 4: Surveiller la CI**

Vérifier `build-linux` (ctest : `config_loadfromstring_tests`, `formulas`, `character_stats_engine_tests`, `unit`, `player` verts) **et** `build-windows` (compilation `server_app` Windows OK). Corriger jusqu'au vert.

---

## Self-Review (effectué)

- **Spec coverage** : courbe XP (T6), 11 stats + cap crit + mêlée pure (T7), embarquement build (T3-T5), Unit/Player UpdateField (T8-T9), migration factions ids courts + empire_hynn selectable + backfill (T10), tests impactés (FormulasTests T6, UnitTests T8, PlayerTests T9, + nouveaux T2/T7). **Reportés assumés** : races.json/corrompus + RaceDefinitionTests/CharacterCustomizationTests + wire + UI + localisation → PR2 ; câblage live → R1. Tracé explicitement (section Périmètre).
- **Placeholders** : aucun TODO/TBD ; code complet par étape.
- **Cohérence des types** : `ComputeStats`/`DerivedStats`/`CharacterStatsTables::FromEmbedded`/`ApplyDerivedStats`/`Sex` cohérents T7→T9 ; symboles générés `kCharacterStatsJson`/`kFactionsJson` cohérents codegen (T5) → consommateurs (T7/T9) ; indices `kUnitField*` cohérents T8.
- **Points à confirmer par l'implémenteur au 1er build** (notés inline) : nom exact de la lib `engine_core` (Config/Log) ; existence/forme de `player_tests` ; nom de l'accesseur `Mask()` dans `Object.h`.
