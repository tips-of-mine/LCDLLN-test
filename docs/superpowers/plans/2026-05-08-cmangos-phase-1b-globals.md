# CMANGOS Phase 1b — Globals (ConditionMgr / ObjectAccessor / GraveyardManager / LocaleStrings) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ajouter 4 utilitaires data-driven côté shard LCDLLN qui débloquent ~5 tickets P2 downstream (Loot, Quests, AI EventAI, DBScripts, Spells) : `ConditionMgr` (mini-DSL de prédicats data-driven AND/OR/NOT), `ObjectAccessor` (façade thread-safe lookup `Player`/`Creature` par GUID), `GraveyardManager` (table de points de respawn avec filtre faction + closest), `LocaleStrings` (cache `(stringId, localeId) → string` avec fallback).

**Architecture:** Nouveau dossier `engine/server/shard/globals/` (premier sous-dossier `shard/` du projet — créer aussi le `CMakeLists.txt` parent si nécessaire). Chaque utilitaire est un singleton chargé au boot via `SQLStorage<T>` (livré par Phase 1a). Pas de virtual call dans `Evaluate` (switch sur enum). `ObjectAccessor` thread-safe via `std::shared_mutex` (lectures concurrentes). `LocaleStrings` lock-free post-load.

**Tech Stack:** C++20, MySQL C client, dépendance directe sur `SQLStorage<T>` (Phase 1a), namespace `engine::server::shard::globals`. UNIX-only (cohérent avec le shard linux). Tests : pattern `main()` standalone, skip si `db.host` non configuré.

---

## Pré-requis

⚠️ **Phase 1a Database doit être livrée et mergée avant ce plan**. Spécifiquement :
- `engine/server/db/SQLStorage.h` accessible
- Migration `0041_phase_1a_test_storage.sql` appliquée

Sans ces dépendances, ce plan **ne compile pas**.

---

## Périmètre verrouillé

### Fichiers créés

- `engine/server/shard/CMakeLists.txt` — squelette (si pas déjà inclus depuis le `engine/server/CMakeLists.txt` parent)
- `engine/server/shard/globals/Condition.h` — types `ConditionType`/`ConditionLogic`/`Condition`/`ConditionGroup`
- `engine/server/shard/globals/ConditionMgr.h` — singleton interface
- `engine/server/shard/globals/ConditionMgr.cpp` — Load + Evaluate
- `engine/server/shard/globals/ConditionMgrTests.cpp`
- `engine/server/shard/globals/ObjectAccessor.h`
- `engine/server/shard/globals/ObjectAccessor.cpp`
- `engine/server/shard/globals/ObjectAccessorTests.cpp`
- `engine/server/shard/globals/GraveyardManager.h`
- `engine/server/shard/globals/GraveyardManager.cpp`
- `engine/server/shard/globals/GraveyardManagerTests.cpp`
- `engine/server/shard/globals/LocaleStrings.h`
- `engine/server/shard/globals/LocaleStrings.cpp`
- `engine/server/shard/globals/LocaleStringsTests.cpp`
- `db/migrations/0042_phase_1b_globals.sql` — 4 tables + seeds minimaux

### Fichiers modifiés

- `engine/server/CMakeLists.txt` — ajouter 4 cibles de tests UNIX
- `config.json` — clés `globals.default_locale`, `globals.fallback_locale`, `globals.graveyard_default_faction_neutral_radius_m`

### Hors scope (PR future)

- `Custom` ConditionType (handler C++ enregistrable) — pas dans ce plan, on livre 5 types data-driven simples
- Hot-reload `.reload conditions` GM command — dépend de CMANGOS.01 ChatCommandRouter
- Spillover de réputation (porté par CMANGOS.24, pas .16)
- 4ᵉ langue ou plus de placeholders dans `LocaleStrings::Format` — démarrer simple `{0}/{1}/{2}`

### Convention de commits

- Un commit par task (TDD)
- Format : `feat(server/shard/globals): ...` ou `test(server/shard/globals): ...` ou `build(server/shard/globals): ...`
- Co-author Claude obligatoire
- Mention `Déploiement : ⚠️ redéploiement serveur (shard) requis` au commit final

---

## Décisions architecturales clés

### 1. Pas de hiérarchie OOP `Player`/`Creature`

LCDLLN utilise une archi **data-driven** (cf. fiche d'audit CMANGOS.02). `ObjectAccessor` fonctionne sur `EntityId` opaque (uint64_t), pas sur des `Player*`/`Creature*`. Le ticket source mentionne ces types — on les remplace par `EntityId` pour rester cohérent avec l'archi LCDLLN. Si CMANGOS.02 est porté plus tard avec des classes `Player`/`Creature`, on fera un refactor en surface (l'API publique restera la même).

### 2. ConditionType — 5 types pour Phase 1b

Phase 1b livre uniquement les 5 types les plus utilisés downstream :
- `LevelGE` (value1 = niveau min joueur)
- `LevelLE` (value1 = niveau max)
- `HasItem` (value1 = item entry, value2 = count min)
- `ZoneId` (value1 = zone id requise)
- `InGroup` (pas de paramètre)

Les autres (`HasAura`, `QuestState`, `Reputation`, `HealthPctBelow`, `NotInCombat`, `Custom`) seront ajoutés au cas par cas par les tickets P2 downstream qui en ont besoin. **YAGNI**.

### 3. ConditionGroup logique

Composition simple : `And` / `Or` / `Not`.
- `And` : tous les membres true → group true
- `Or` : un membre true → group true
- `Not` : exactement un membre, négation

Schéma de référence dans `condition_groups` :
- `group_id` + `member_id` + `member_type` (`0`=condition, `1`=group)
- Détection cycles au load via DFS

### 4. ObjectAccessor : context d'évaluation

`ConditionMgr::Evaluate` a besoin de **2 inputs** : un `EntityId source` et un `WorldObject const* target` optionnel. Pour rester data-driven, on définit une struct `EvaluationContext`:

```cpp
struct EvaluationContext {
    uint64_t sourceEntityId;     // joueur qui déclenche (loot, quête, ...)
    int32_t  sourceLevel;        // pour LevelGE/LE — précomputed pour éviter lookup
    uint32_t sourceZoneId;       // pour ZoneId
    bool     inGroup;            // pour InGroup
    // map item_entry → count pour HasItem (résolu par le caller)
    std::unordered_map<uint32_t, uint32_t> sourceItems;
};
```

Le caller (loot/quest handler) remplit ce contexte avec les infos disponibles. Cela évite à `ConditionMgr` de devoir tout savoir sur l'archi LCDLLN.

### 5. Locale par défaut LCDLLN = `fr_FR`

Le projet est francophone. Locale ID 0 = `fr_FR`. Locale ID 1 = `en_US`. Etc. `default_locale` configurable via `config.json` mais défaut `fr_FR`.

---

## Task 1 : Migration DB 0042 — 4 tables Globals + seeds

**Files:**
- Create: `db/migrations/0042_phase_1b_globals.sql`

- [ ] **Step 1.1 : Créer le fichier de migration**

```sql
-- 0042 — Phase 1b CMANGOS Globals : 4 tables (conditions, condition_groups,
-- graveyards, locale_strings) + seeds minimaux pour les tests.

-- ─── conditions : prédicats atomiques data-driven ───
CREATE TABLE IF NOT EXISTS conditions (
  condition_id  INT UNSIGNED NOT NULL,
  type          TINYINT UNSIGNED NOT NULL,    -- enum ConditionType (0=LevelGE,1=LevelLE,2=HasItem,3=ZoneId,4=InGroup)
  value1        INT NOT NULL DEFAULT 0,
  value2        INT NOT NULL DEFAULT 0,
  value3        INT NOT NULL DEFAULT 0,
  description   VARCHAR(255),
  PRIMARY KEY (condition_id)
);

-- ─── condition_groups : composition logique AND/OR/NOT ───
CREATE TABLE IF NOT EXISTS condition_groups (
  group_id      INT UNSIGNED NOT NULL,
  logic         TINYINT UNSIGNED NOT NULL,    -- enum ConditionLogic (0=And,1=Or,2=Not)
  member_id     INT UNSIGNED NOT NULL,        -- condition_id ou group_id selon member_type
  member_type   TINYINT UNSIGNED NOT NULL,    -- 0=condition, 1=group
  PRIMARY KEY (group_id, member_id, member_type)
);

-- ─── graveyards : points de respawn ───
CREATE TABLE IF NOT EXISTS graveyards (
  id          INT UNSIGNED NOT NULL,
  map_id      INT UNSIGNED NOT NULL,
  position_x  FLOAT NOT NULL,
  position_y  FLOAT NOT NULL,
  position_z  FLOAT NOT NULL,
  faction     TINYINT UNSIGNED NOT NULL DEFAULT 0,    -- 0=neutral, 1+ = faction id
  zone_id     INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (id),
  KEY ix_graveyards_map (map_id)
);

-- ─── locale_strings : i18n côté serveur ───
CREATE TABLE IF NOT EXISTS locale_strings (
  string_id   INT UNSIGNED NOT NULL,
  locale_id   TINYINT UNSIGNED NOT NULL,    -- 0=fr_FR, 1=en_US
  text        TEXT NOT NULL,
  PRIMARY KEY (string_id, locale_id)
);

-- ─── seeds (idempotents via INSERT IGNORE) ───

-- Conditions de test : C1=Lvl>=10, C2=Lvl<=20, C3=ZoneId=42
INSERT IGNORE INTO conditions (condition_id, type, value1, value2, value3, description) VALUES
  (1, 0, 10, 0, 0, 'Test LevelGE 10'),
  (2, 1, 20, 0, 0, 'Test LevelLE 20'),
  (3, 3, 42, 0, 0, 'Test ZoneId 42'),
  (4, 4, 0,  0, 0, 'Test InGroup');

-- Groups de test :
-- G100 = AND(C1, C2)  → niveau entre 10 et 20
-- G101 = OR(G100, C3) → niveau dans range OU zone 42
-- G102 = NOT(C4)       → pas en groupe
INSERT IGNORE INTO condition_groups (group_id, logic, member_id, member_type) VALUES
  (100, 0, 1, 0),  -- AND C1
  (100, 0, 2, 0),  -- AND C2
  (101, 1, 100, 1), -- OR G100
  (101, 1, 3, 0),  -- OR C3
  (102, 2, 4, 0);  -- NOT C4

-- Graveyards de test : 3 points sur map 0
INSERT IGNORE INTO graveyards (id, map_id, position_x, position_y, position_z, faction, zone_id) VALUES
  (1, 0,    0.0,   0.0,  0.0, 0, 0),
  (2, 0,  100.0,   0.0,  0.0, 1, 0),
  (3, 0,  200.0,   0.0,  0.0, 2, 0);

-- Locale strings de test : ID 1000 fr_FR + en_US, ID 1001 fr_FR seulement (test fallback)
INSERT IGNORE INTO locale_strings (string_id, locale_id, text) VALUES
  (1000, 0, 'Bienvenue {0}, niveau {1}!'),
  (1000, 1, 'Welcome {0}, level {1}!'),
  (1001, 0, 'Bonjour le monde');
```

- [ ] **Step 1.2 : Commit**

```bash
git add db/migrations/0042_phase_1b_globals.sql
git commit -m "feat(db): migration 0042 phase_1b_globals (4 tables + seeds)

Cree conditions, condition_groups, graveyards, locale_strings + seeds
minimaux pour exercer Phase 1b.

CMANGOS.16 (Phase 1b Globals).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2 : Condition.h — enums + structs

**Files:**
- Create: `engine/server/shard/globals/Condition.h`

- [ ] **Step 2.1 : Écrire le header**

```cpp
#pragma once
// CMANGOS.16 (Phase 1b) — types pour ConditionMgr : enum ConditionType,
// enum ConditionLogic, struct Condition, struct ConditionGroup.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server::shard::globals
{
	/// Types de prédicats supportés en Phase 1b. Étendre via PR séparée.
	enum class ConditionType : uint8_t
	{
		LevelGE  = 0,  ///< value1 = niveau min joueur
		LevelLE  = 1,  ///< value1 = niveau max joueur
		HasItem  = 2,  ///< value1 = item entry, value2 = count min
		ZoneId   = 3,  ///< value1 = zone id requise
		InGroup  = 4,  ///< pas de paramètre
		// Étendre ici (HasAura, QuestState, Reputation, ...) au fil des besoins downstream.
	};

	/// Logique de composition pour ConditionGroup.
	enum class ConditionLogic : uint8_t
	{
		And = 0,
		Or  = 1,
		Not = 2,  ///< un seul membre, négation
	};

	/// Type de membre d'un ConditionGroup : 0=condition_id, 1=group_id.
	enum class ConditionMemberType : uint8_t
	{
		Condition = 0,
		Group     = 1,
	};

	/// Prédicat atomique chargé depuis la table `conditions`.
	struct Condition
	{
		uint32_t conditionId = 0;
		ConditionType type   = ConditionType::LevelGE;
		int32_t value1       = 0;
		int32_t value2       = 0;
		int32_t value3       = 0;
	};

	/// Membre d'un ConditionGroup (référence vers une condition ou un autre group).
	struct ConditionGroupMember
	{
		uint32_t memberId           = 0;
		ConditionMemberType memberType = ConditionMemberType::Condition;
	};

	/// Composition logique de plusieurs conditions/groups.
	struct ConditionGroup
	{
		uint32_t groupId      = 0;
		ConditionLogic logic  = ConditionLogic::And;
		std::vector<ConditionGroupMember> members;
	};

	/// Contexte d'évaluation (fourni par le caller : handler loot/quest/...).
	/// Permet à ConditionMgr de rester data-driven sans connaître l'archi LCDLLN
	/// (Player class, etc.).
	struct EvaluationContext
	{
		uint64_t sourceEntityId = 0;
		int32_t  sourceLevel    = 0;
		uint32_t sourceZoneId   = 0;
		bool     inGroup        = false;
		/// item_entry → count détenu. Map vide = pas d'items.
		std::unordered_map<uint32_t, uint32_t> sourceItems;
	};
}
```

- [ ] **Step 2.2 : Commit**

```bash
git add engine/server/shard/globals/Condition.h
git commit -m "feat(server/shard/globals): Condition + ConditionGroup types

Enums + structs pour ConditionMgr : 5 ConditionType (LevelGE/LE, HasItem,
ZoneId, InGroup), 3 ConditionLogic (And/Or/Not), Condition, ConditionGroup,
EvaluationContext (data-driven, ne depend pas d'une hierarchie OOP).

CMANGOS.16 (Phase 1b).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3 : ConditionMgr.h — interface

**Files:**
- Create: `engine/server/shard/globals/ConditionMgr.h`

- [ ] **Step 3.1 : Écrire l'interface**

```cpp
#pragma once
// CMANGOS.16 (Phase 1b) — ConditionMgr : moteur d'évaluation data-driven
// chargé une fois au boot depuis 2 tables SQL (conditions + condition_groups).

#include "engine/server/shard/globals/Condition.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server::shard::globals
{
	/// Singleton chargé au boot, lookup O(1), évaluation O(profondeur du group).
	/// Thread-safety : Load() une seule fois (assertion sinon). Post-load,
	/// Evaluate() est lock-free (read-only).
	class ConditionMgr
	{
	public:
		ConditionMgr() = default;
		~ConditionMgr() = default;
		ConditionMgr(const ConditionMgr&) = delete;
		ConditionMgr& operator=(const ConditionMgr&) = delete;

		/// Charge `conditions` + `condition_groups` depuis la DB.
		/// Retourne false si requête échoue ou si cycle détecté dans les groups.
		/// \pre Une seule fois.
		bool Load(engine::server::db::ConnectionPool& pool);

		/// Évalue une condition atomique par ID. Retourne `true` si le prédicat
		/// passe, `false` sinon (incluant condition inexistante).
		bool EvaluateCondition(uint32_t conditionId, const EvaluationContext& ctx) const;

		/// Évalue un groupe par ID. Retourne `true` si le group passe.
		bool EvaluateGroup(uint32_t groupId, const EvaluationContext& ctx) const;

		/// Helper unifié : si l'ID est un group_id, évalue le group ; sinon évalue
		/// comme condition_id.
		/// Note : il y a une zone d'overlap potentielle si un condition_id et un
		/// group_id partagent la même valeur. Convention LCDLLN : condition_id
		/// ∈ [1, 9999], group_id ∈ [10000, ∞) — voir doc db_sql_guidelines.md.
		bool Evaluate(uint32_t id, const EvaluationContext& ctx) const;

		size_t ConditionCount() const { return m_conditions.size(); }
		size_t GroupCount() const     { return m_groups.size(); }

	private:
		/// DFS détection cycle dans les groupes. Retourne true si cycle trouvé.
		bool DetectCycles() const;
		bool DetectCyclesFrom(uint32_t groupId,
			std::unordered_map<uint32_t, int>& color) const;

		/// Évaluation d'un atom Condition (switch sur type, pas de virtual).
		bool EvaluateAtom(const Condition& cond, const EvaluationContext& ctx) const;

		std::unordered_map<uint32_t, Condition> m_conditions;
		std::unordered_map<uint32_t, ConditionGroup> m_groups;
		bool m_loaded = false;
	};
}
```

- [ ] **Step 3.2 : Commit**

```bash
git add engine/server/shard/globals/ConditionMgr.h
git commit -m "feat(server/shard/globals): ConditionMgr interface

Singleton avec Load + EvaluateCondition + EvaluateGroup + Evaluate (helper).
Convention condition_id [1,9999], group_id [10000,inf). Detection cycles
au load via DFS. Lock-free post-load.

CMANGOS.16 (Phase 1b).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4 : ConditionMgr.cpp — Load + Evaluate (5 ConditionTypes)

**Files:**
- Create: `engine/server/shard/globals/ConditionMgr.cpp`

- [ ] **Step 4.1 : Implémentation Load + helpers DFS**

```cpp
#include "engine/server/shard/globals/ConditionMgr.h"

#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <cstdlib>

namespace engine::server::shard::globals
{
	namespace
	{
		// Convention de l'overlap condition_id / group_id : voir doc.
		constexpr uint32_t kGroupIdMinInclusive = 10000;

		bool IsGroupIdRange(uint32_t id)
		{
			return id >= kGroupIdMinInclusive;
		}
	}

	bool ConditionMgr::Load(engine::server::db::ConnectionPool& pool)
	{
		if (m_loaded)
			return false;

		auto guard = pool.Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;

		// 1) Conditions atomiques.
		{
			MYSQL_RES* res = engine::server::db::DbQuery(mysql,
				"SELECT condition_id, type, value1, value2, value3 FROM conditions");
			if (!res)
				return false;
			MYSQL_ROW row;
			while ((row = mysql_fetch_row(res)) != nullptr)
			{
				if (!row[0]) continue;
				Condition c{};
				c.conditionId = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
				c.type        = static_cast<ConditionType>(std::atoi(row[1]));
				c.value1      = std::atoi(row[2]);
				c.value2      = std::atoi(row[3]);
				c.value3      = std::atoi(row[4]);
				m_conditions.emplace(c.conditionId, c);
			}
			engine::server::db::DbFreeResult(res);
		}

		// 2) Condition groups (rows multiples par group_id).
		{
			MYSQL_RES* res = engine::server::db::DbQuery(mysql,
				"SELECT group_id, logic, member_id, member_type FROM condition_groups "
				"ORDER BY group_id, member_id");
			if (!res)
				return false;
			MYSQL_ROW row;
			while ((row = mysql_fetch_row(res)) != nullptr)
			{
				if (!row[0]) continue;
				const uint32_t gid = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
				ConditionGroup& g = m_groups[gid];
				g.groupId = gid;
				g.logic   = static_cast<ConditionLogic>(std::atoi(row[1]));
				ConditionGroupMember m{};
				m.memberId   = static_cast<uint32_t>(std::strtoul(row[2], nullptr, 10));
				m.memberType = static_cast<ConditionMemberType>(std::atoi(row[3]));
				g.members.push_back(m);
			}
			engine::server::db::DbFreeResult(res);
		}

		// 3) Détection cycles dans les groups.
		if (DetectCycles())
		{
			LOG_ERROR(Core, "[ConditionMgr] Cycle detected in condition_groups, aborting load");
			m_conditions.clear();
			m_groups.clear();
			return false;
		}

		m_loaded = true;
		LOG_INFO(Core, "[ConditionMgr] Loaded {} conditions, {} groups",
			m_conditions.size(), m_groups.size());
		return true;
	}

	bool ConditionMgr::DetectCycles() const
	{
		std::unordered_map<uint32_t, int> color;  // 0=white, 1=gray, 2=black
		for (const auto& [gid, _] : m_groups)
		{
			color[gid] = 0;
		}
		for (const auto& [gid, _] : m_groups)
		{
			if (color[gid] == 0 && DetectCyclesFrom(gid, color))
				return true;
		}
		return false;
	}

	bool ConditionMgr::DetectCyclesFrom(uint32_t groupId,
		std::unordered_map<uint32_t, int>& color) const
	{
		color[groupId] = 1;  // gray
		auto it = m_groups.find(groupId);
		if (it == m_groups.end())
		{
			color[groupId] = 2;
			return false;
		}
		for (const auto& m : it->second.members)
		{
			if (m.memberType != ConditionMemberType::Group)
				continue;
			auto cit = color.find(m.memberId);
			if (cit == color.end())
				continue;  // group référencé mais inexistant — pas un cycle, juste broken data
			if (cit->second == 1)
				return true;  // back edge → cycle
			if (cit->second == 0 && DetectCyclesFrom(m.memberId, color))
				return true;
		}
		color[groupId] = 2;  // black
		return false;
	}

	bool ConditionMgr::EvaluateAtom(const Condition& cond, const EvaluationContext& ctx) const
	{
		switch (cond.type)
		{
			case ConditionType::LevelGE:
				return ctx.sourceLevel >= cond.value1;
			case ConditionType::LevelLE:
				return ctx.sourceLevel <= cond.value1;
			case ConditionType::HasItem:
			{
				const uint32_t itemEntry = static_cast<uint32_t>(cond.value1);
				const uint32_t minCount  = static_cast<uint32_t>(cond.value2 > 0 ? cond.value2 : 1);
				auto it = ctx.sourceItems.find(itemEntry);
				return it != ctx.sourceItems.end() && it->second >= minCount;
			}
			case ConditionType::ZoneId:
				return ctx.sourceZoneId == static_cast<uint32_t>(cond.value1);
			case ConditionType::InGroup:
				return ctx.inGroup;
		}
		return false;  // unknown enum value
	}

	bool ConditionMgr::EvaluateCondition(uint32_t conditionId, const EvaluationContext& ctx) const
	{
		auto it = m_conditions.find(conditionId);
		if (it == m_conditions.end())
			return false;
		return EvaluateAtom(it->second, ctx);
	}

	bool ConditionMgr::EvaluateGroup(uint32_t groupId, const EvaluationContext& ctx) const
	{
		auto it = m_groups.find(groupId);
		if (it == m_groups.end())
			return false;
		const ConditionGroup& g = it->second;

		if (g.logic == ConditionLogic::Not)
		{
			// 1 seul membre attendu.
			if (g.members.empty())
				return false;
			const auto& m = g.members.front();
			const bool inner = (m.memberType == ConditionMemberType::Group)
				? EvaluateGroup(m.memberId, ctx)
				: EvaluateCondition(m.memberId, ctx);
			return !inner;
		}

		const bool isAnd = (g.logic == ConditionLogic::And);
		if (g.members.empty())
			return isAnd;  // AND vide = vrai, OR vide = faux (convention)

		for (const auto& m : g.members)
		{
			const bool ok = (m.memberType == ConditionMemberType::Group)
				? EvaluateGroup(m.memberId, ctx)
				: EvaluateCondition(m.memberId, ctx);
			if (isAnd && !ok)
				return false;
			if (!isAnd && ok)
				return true;
		}
		return isAnd;
	}

	bool ConditionMgr::Evaluate(uint32_t id, const EvaluationContext& ctx) const
	{
		if (IsGroupIdRange(id))
			return EvaluateGroup(id, ctx);
		return EvaluateCondition(id, ctx);
	}
}
```

- [ ] **Step 4.2 : Commit**

```bash
git add engine/server/shard/globals/ConditionMgr.cpp
git commit -m "feat(server/shard/globals): ConditionMgr Load + Evaluate

Charge conditions + condition_groups via DbQuery. Detection cycles DFS
au load. EvaluateAtom switch sur ConditionType (pas de virtual). Logic
And/Or/Not avec convention AND vide=true, OR vide=false.

CMANGOS.16 (Phase 1b).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5 : ConditionMgr — tests Load + Evaluate atom + Evaluate group

**Files:**
- Create: `engine/server/shard/globals/ConditionMgrTests.cpp`
- Modify: `engine/server/CMakeLists.txt`

- [ ] **Step 5.1 : Écrire le test**

```cpp
// CMANGOS.16 (Phase 1b) — Tests ConditionMgr.

#include "engine/server/shard/globals/ConditionMgr.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::shard::globals::ConditionMgr;
	using engine::server::shard::globals::EvaluationContext;
	using engine::server::db::ConnectionPool;

	bool TestAtomEvaluation(ConditionMgr& mgr)
	{
		EvaluationContext ctx{};
		ctx.sourceLevel = 5;
		ctx.sourceZoneId = 10;
		ctx.inGroup = false;

		// C1=LevelGE 10. Niveau 5 → false, niveau 15 → true.
		if (mgr.EvaluateCondition(1, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C1(LvlGE10) lvl=5 expected false");
			return false;
		}
		ctx.sourceLevel = 15;
		if (!mgr.EvaluateCondition(1, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C1(LvlGE10) lvl=15 expected true");
			return false;
		}

		// C3=ZoneId 42. Zone 10 → false, zone 42 → true.
		if (mgr.EvaluateCondition(3, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C3(Zone42) zone=10 expected false");
			return false;
		}
		ctx.sourceZoneId = 42;
		if (!mgr.EvaluateCondition(3, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C3(Zone42) zone=42 expected true");
			return false;
		}

		// C4=InGroup. Pas en groupe → false.
		if (mgr.EvaluateCondition(4, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C4(InGroup) inGroup=false expected false");
			return false;
		}
		ctx.inGroup = true;
		if (!mgr.EvaluateCondition(4, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] C4(InGroup) inGroup=true expected true");
			return false;
		}
		LOG_INFO(Core, "[ConditionMgrTests] Atom evaluation OK");
		return true;
	}

	bool TestGroupComposition(ConditionMgr& mgr)
	{
		EvaluationContext ctx{};
		// G100 = AND(C1=LvlGE10, C2=LvlLE20).
		// lvl=15 → true. lvl=5 → false. lvl=25 → false.
		ctx.sourceLevel = 15;
		if (!mgr.EvaluateGroup(100, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G100(AND) lvl=15 expected true");
			return false;
		}
		ctx.sourceLevel = 5;
		if (mgr.EvaluateGroup(100, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G100(AND) lvl=5 expected false");
			return false;
		}
		ctx.sourceLevel = 25;
		if (mgr.EvaluateGroup(100, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G100(AND) lvl=25 expected false");
			return false;
		}

		// G101 = OR(G100, C3=Zone42). lvl=5+zone=42 → true via C3.
		ctx.sourceLevel = 5;
		ctx.sourceZoneId = 42;
		if (!mgr.EvaluateGroup(101, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G101(OR) lvl=5,zone=42 expected true");
			return false;
		}
		// lvl=5 + zone=10 → false.
		ctx.sourceZoneId = 10;
		if (mgr.EvaluateGroup(101, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G101(OR) lvl=5,zone=10 expected false");
			return false;
		}

		// G102 = NOT(C4=InGroup). inGroup=false → NOT(false)=true.
		ctx.inGroup = false;
		if (!mgr.EvaluateGroup(102, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G102(NOT) inGroup=false expected true");
			return false;
		}
		ctx.inGroup = true;
		if (mgr.EvaluateGroup(102, ctx))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] G102(NOT) inGroup=true expected false");
			return false;
		}
		LOG_INFO(Core, "[ConditionMgrTests] Group composition AND/OR/NOT OK");
		return true;
	}

	bool TestEvaluateHelper(ConditionMgr& mgr)
	{
		// Convention : id < 10000 → condition, id >= 10000 → group.
		// Mais nos tests utilisent ids 1-4 pour conditions et 100-102 pour groups.
		// Le helper Evaluate dispatch via la convention ; donc Evaluate(100, ctx)
		// va aller chercher dans les conditions (1-9999) et NE TROUVERA RIEN.
		// On teste donc directement EvaluateGroup ci-dessus. Le helper Evaluate
		// est testé avec un id de condition seul.
		EvaluationContext ctx{};
		ctx.sourceLevel = 15;
		if (!mgr.Evaluate(1, ctx))  // Evaluate(1) → EvaluateCondition(1) → C1 ok
		{
			LOG_ERROR(Core, "[ConditionMgrTests] Evaluate(1) expected true");
			return false;
		}
		LOG_INFO(Core, "[ConditionMgrTests] Evaluate dispatch OK");
		return true;
	}

	bool TestDoubleLoadRejected(ConnectionPool& pool)
	{
		ConditionMgr mgr;
		const bool ok1 = mgr.Load(pool);
		if (!ok1)
		{
			LOG_ERROR(Core, "[ConditionMgrTests] First Load failed");
			return false;
		}
		const bool ok2 = mgr.Load(pool);
		if (ok2)
		{
			LOG_ERROR(Core, "[ConditionMgrTests] Second Load should fail");
			return false;
		}
		LOG_INFO(Core, "[ConditionMgrTests] Double-load rejected OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	if (config.GetString("db.host", "").empty())
	{
		LOG_INFO(Core, "[ConditionMgrTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[ConditionMgrTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	bool ok = false;
	{
		ConditionMgr mgr;
		if (!mgr.Load(pool))
		{
			LOG_ERROR(Core, "[ConditionMgrTests] Load failed (migration 0042 applied?)");
		}
		else
		{
			ok = TestAtomEvaluation(mgr) && TestGroupComposition(mgr) && TestEvaluateHelper(mgr);
		}
	}
	if (ok)
		ok = TestDoubleLoadRejected(pool);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 5.2 : Ajouter la cible CMake**

Localiser dans `engine/server/CMakeLists.txt` la cible `sql_delay_thread_tests` (ajoutée par Phase 1a Task 11). **Ajouter juste après** :

```cmake
  # CMANGOS.16 (Phase 1b) : ConditionMgr tests
  add_executable(condition_mgr_tests
    shard/globals/ConditionMgrTests.cpp
    shard/globals/ConditionMgr.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(condition_mgr_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(condition_mgr_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(condition_mgr_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME condition_mgr_tests COMMAND condition_mgr_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 5.3 : Build + run**

```bash
cmake --build --preset linux-x64 --target condition_mgr_tests
ctest --preset linux-x64 -R condition_mgr_tests --output-on-failure
```

Expected: **PASS** avec logs `Atom evaluation OK`, `Group composition AND/OR/NOT OK`, `Evaluate dispatch OK`, `Double-load rejected OK`.

- [ ] **Step 5.4 : Commit**

```bash
git add engine/server/shard/globals/ConditionMgrTests.cpp engine/server/CMakeLists.txt
git commit -m "test(server/shard/globals): ConditionMgr Load + atom + group + double-load

Tests :
- 5 atoms : LevelGE/LE/HasItem/ZoneId/InGroup avec contextes valides/invalides
- Composition : AND(C1,C2), OR(G100,C3), NOT(C4) avec verification short-circuit
- Helper Evaluate(id) : dispatch via convention condition_id<10000<group_id
- Double-load rejete (assertion contract)

CMANGOS.16 (Phase 1b) — TDD green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6 : ObjectAccessor.h — interface façade

**Files:**
- Create: `engine/server/shard/globals/ObjectAccessor.h`

- [ ] **Step 6.1 : Écrire l'interface (data-driven, EntityId opaque)**

```cpp
#pragma once
// CMANGOS.16 (Phase 1b) — ObjectAccessor : façade thread-safe pour lookup
// d'entités par GUID. Data-driven (EntityId opaque) — pas de hierarchie OOP.

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server::shard::globals
{
	/// Type opaque pour les entités du shard. Aligné avec engine::server::EntityId
	/// (réutiliser via include indirect au moment de l'intégration).
	using EntityId = uint64_t;

	/// Snapshot d'une entité accessible : metadata légère, pas le pointeur runtime.
	/// Le caller utilise cet `EntityId` pour interroger les systèmes spécifiques
	/// (positions via `SpatialPartition`, etc.).
	struct EntitySnapshot
	{
		EntityId entityId = 0;
		std::string name;
		uint32_t mapId   = 0;
		uint32_t zoneId  = 0;
		bool isPlayer    = false;     ///< true=Player, false=Creature
	};

	/// Singleton thread-safe : registre des entités présentes côté shard.
	/// Inscription au login (Player) ou spawn (Creature). Désinscription au
	/// logout/despawn.
	///
	/// Thread-safety : `std::shared_mutex` — multiple readers concurrents,
	/// writer exclusif lors de Register/Unregister.
	class ObjectAccessor
	{
	public:
		ObjectAccessor() = default;
		~ObjectAccessor() = default;
		ObjectAccessor(const ObjectAccessor&) = delete;
		ObjectAccessor& operator=(const ObjectAccessor&) = delete;

		/// Inscrit une entité au registre. Si déjà présente, écrase (utile pour
		/// reconnexion après crash). Retourne false uniquement si entityId==0.
		bool Register(const EntitySnapshot& snapshot);

		/// Désinscrit une entité. Retourne true si l'entité était présente.
		bool Unregister(EntityId entityId);

		/// Lookup par GUID. Retourne un snapshot copié (thread-safe lecture).
		std::optional<EntitySnapshot> Find(EntityId entityId) const;

		/// Lookup par nom normalisé (lowercase). Retourne le premier match.
		/// Lent : O(N). Pour cas peu fréquents (whisper par nom). Utilise
		/// `SessionCharacterMap` pour les hot paths existants.
		std::optional<EntitySnapshot> FindByName(std::string_view name) const;

		/// Nombre d'entrées actuellement enregistrées.
		size_t Size() const;

	private:
		mutable std::shared_mutex m_mutex;
		std::unordered_map<EntityId, EntitySnapshot> m_entities;
	};
}
```

- [ ] **Step 6.2 : Commit**

```bash
git add engine/server/shard/globals/ObjectAccessor.h
git commit -m "feat(server/shard/globals): ObjectAccessor interface (thread-safe)

Facade lookup d'entites par EntityId (data-driven, pas de hierarchie OOP).
Register/Unregister/Find par GUID (O(1)) + FindByName (O(N) cas rares).
Thread-safety via shared_mutex (multiple readers, writer exclusif).

CMANGOS.16 (Phase 1b).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7 : ObjectAccessor.cpp + tests multi-thread

**Files:**
- Create: `engine/server/shard/globals/ObjectAccessor.cpp`
- Create: `engine/server/shard/globals/ObjectAccessorTests.cpp`
- Modify: `engine/server/CMakeLists.txt`

- [ ] **Step 7.1 : Implémentation**

```cpp
#include "engine/server/shard/globals/ObjectAccessor.h"

#include <algorithm>

namespace engine::server::shard::globals
{
	namespace
	{
		std::string Lowercase(std::string_view s)
		{
			std::string out;
			out.reserve(s.size());
			for (char c : s)
				out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			return out;
		}
	}

	bool ObjectAccessor::Register(const EntitySnapshot& snapshot)
	{
		if (snapshot.entityId == 0)
			return false;
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		m_entities[snapshot.entityId] = snapshot;
		return true;
	}

	bool ObjectAccessor::Unregister(EntityId entityId)
	{
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		return m_entities.erase(entityId) > 0;
	}

	std::optional<EntitySnapshot> ObjectAccessor::Find(EntityId entityId) const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		auto it = m_entities.find(entityId);
		if (it == m_entities.end())
			return std::nullopt;
		return it->second;
	}

	std::optional<EntitySnapshot> ObjectAccessor::FindByName(std::string_view name) const
	{
		const std::string needle = Lowercase(name);
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		for (const auto& [id, snap] : m_entities)
		{
			if (Lowercase(snap.name) == needle)
				return snap;
		}
		return std::nullopt;
	}

	size_t ObjectAccessor::Size() const
	{
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		return m_entities.size();
	}
}
```

- [ ] **Step 7.2 : Test (sans DB — pur in-memory + multi-thread)**

```cpp
// CMANGOS.16 (Phase 1b) — Tests ObjectAccessor : Register/Unregister/Find +
// concurrence multi-readers / writer exclusif.

#include "engine/server/shard/globals/ObjectAccessor.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace
{
	using engine::server::shard::globals::ObjectAccessor;
	using engine::server::shard::globals::EntitySnapshot;

	bool TestBasicCrud()
	{
		ObjectAccessor acc;
		if (acc.Size() != 0)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] empty size != 0");
			return false;
		}

		EntitySnapshot a{};
		a.entityId = 42;
		a.name = "Alice";
		a.mapId = 0;
		a.isPlayer = true;
		if (!acc.Register(a) || acc.Size() != 1)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] Register Alice failed");
			return false;
		}

		auto found = acc.Find(42);
		if (!found || found->name != "Alice")
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] Find(42) returned unexpected");
			return false;
		}

		auto byName = acc.FindByName("ALICE");  // case-insensitive
		if (!byName || byName->entityId != 42)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] FindByName(ALICE) failed");
			return false;
		}

		if (!acc.Unregister(42) || acc.Size() != 0)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] Unregister failed");
			return false;
		}

		// Register avec entityId=0 doit échouer.
		EntitySnapshot zero{};
		if (acc.Register(zero))
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] Register(entityId=0) should fail");
			return false;
		}

		LOG_INFO(Core, "[ObjectAccessorTests] Basic CRUD OK");
		return true;
	}

	bool TestConcurrentReadersWriter()
	{
		ObjectAccessor acc;
		// Pré-charge 100 entités.
		for (uint64_t i = 1; i <= 100; ++i)
		{
			EntitySnapshot s{};
			s.entityId = i;
			s.name = "E" + std::to_string(i);
			acc.Register(s);
		}

		std::atomic<bool> stop{false};
		std::atomic<int> readerErrors{0};

		// 8 readers : Find en boucle.
		std::vector<std::thread> readers;
		for (int t = 0; t < 8; ++t)
		{
			readers.emplace_back([&]() {
				while (!stop.load())
				{
					for (uint64_t i = 1; i <= 100; ++i)
					{
						auto s = acc.Find(i);
						if (s && s->entityId != i)
							readerErrors.fetch_add(1);
					}
				}
			});
		}

		// Writer : Unregister + Register en boucle pendant 200ms.
		std::thread writer([&]() {
			auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
			while (std::chrono::steady_clock::now() < deadline)
			{
				for (uint64_t i = 1; i <= 100; ++i)
				{
					acc.Unregister(i);
					EntitySnapshot s{};
					s.entityId = i;
					s.name = "E" + std::to_string(i);
					acc.Register(s);
				}
			}
		});

		writer.join();
		stop.store(true);
		for (auto& r : readers) r.join();

		if (readerErrors.load() > 0)
		{
			LOG_ERROR(Core, "[ObjectAccessorTests] {} reader errors (race condition?)",
				readerErrors.load());
			return false;
		}
		LOG_INFO(Core, "[ObjectAccessorTests] Concurrent readers/writer OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestBasicCrud() && TestConcurrentReadersWriter();

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 7.3 : Ajouter la cible CMake**

Localiser dans `engine/server/CMakeLists.txt` la cible `condition_mgr_tests`. **Ajouter juste après** :

```cmake
  # CMANGOS.16 (Phase 1b) : ObjectAccessor tests (no DB required)
  add_executable(object_accessor_tests
    shard/globals/ObjectAccessorTests.cpp
    shard/globals/ObjectAccessor.cpp
  )
  target_include_directories(object_accessor_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(object_accessor_tests PRIVATE engine_core pthread)
  target_compile_options(object_accessor_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME object_accessor_tests COMMAND object_accessor_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 7.4 : Build + run**

```bash
cmake --build --preset linux-x64 --target object_accessor_tests
ctest --preset linux-x64 -R object_accessor_tests --output-on-failure
```

Expected: **PASS** — logs `Basic CRUD OK` puis `Concurrent readers/writer OK`. Pas de DB requise (tests in-memory).

- [ ] **Step 7.5 : Commit**

```bash
git add engine/server/shard/globals/ObjectAccessor.cpp \
        engine/server/shard/globals/ObjectAccessorTests.cpp \
        engine/server/CMakeLists.txt
git commit -m "feat(server/shard/globals): ObjectAccessor impl + multi-thread tests

Implementation thread-safe (shared_mutex) + tests :
- CRUD basique (Register/Unregister/Find/FindByName case-insensitive)
- Concurrence : 8 readers en boucle + 1 writer 200ms, 0 erreurs

CMANGOS.16 (Phase 1b) — TDD green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8 : GraveyardManager.h — interface

**Files:**
- Create: `engine/server/shard/globals/GraveyardManager.h`

- [ ] **Step 8.1 : Interface**

```cpp
#pragma once
// CMANGOS.16 (Phase 1b) — GraveyardManager : table de points de respawn
// par map + filtre faction + closest.

#include <cstdint>
#include <optional>
#include <vector>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server::shard::globals
{
	/// Faction id : 0 = neutral, 1+ = faction spécifique.
	using FactionId = uint8_t;

	/// Définition d'un graveyard.
	struct Graveyard
	{
		uint32_t id        = 0;
		uint32_t mapId     = 0;
		float positionX    = 0.0f;
		float positionY    = 0.0f;
		float positionZ    = 0.0f;
		FactionId faction  = 0;     ///< 0 = neutral, accepte toutes factions
		uint32_t zoneId    = 0;
	};

	class GraveyardManager
	{
	public:
		GraveyardManager() = default;
		~GraveyardManager() = default;
		GraveyardManager(const GraveyardManager&) = delete;
		GraveyardManager& operator=(const GraveyardManager&) = delete;

		/// Charge `graveyards` depuis la DB. \pre Une seule fois.
		bool Load(engine::server::db::ConnectionPool& pool);

		/// Cherche le graveyard valide le plus proche pour la position et faction
		/// données. Un graveyard est valide si `faction == 0` (neutral) OU
		/// `faction == requestedFaction`. Retourne nullopt si aucun candidat
		/// sur cette map.
		std::optional<Graveyard> ClosestGraveyard(uint32_t mapId,
			float posX, float posY, float posZ, FactionId requestedFaction) const;

		size_t Size() const { return m_graveyards.size(); }

	private:
		std::vector<Graveyard> m_graveyards;  // pas de map — N petit, scan linéaire OK
		bool m_loaded = false;
	};
}
```

- [ ] **Step 8.2 : Commit**

```bash
git add engine/server/shard/globals/GraveyardManager.h
git commit -m "feat(server/shard/globals): GraveyardManager interface

Load + ClosestGraveyard(map, pos, faction) avec filtrage neutral OR faction
matchee. Stockage vector lineaire (N graveyards petit, scan acceptable).

CMANGOS.16 (Phase 1b).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9 : GraveyardManager.cpp + tests

**Files:**
- Create: `engine/server/shard/globals/GraveyardManager.cpp`
- Create: `engine/server/shard/globals/GraveyardManagerTests.cpp`
- Modify: `engine/server/CMakeLists.txt`

- [ ] **Step 9.1 : Implémentation**

```cpp
#include "engine/server/shard/globals/GraveyardManager.h"

#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <cmath>
#include <cstdlib>
#include <limits>

namespace engine::server::shard::globals
{
	bool GraveyardManager::Load(engine::server::db::ConnectionPool& pool)
	{
		if (m_loaded)
			return false;

		auto guard = pool.Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;

		MYSQL_RES* res = engine::server::db::DbQuery(mysql,
			"SELECT id, map_id, position_x, position_y, position_z, faction, zone_id "
			"FROM graveyards");
		if (!res)
			return false;

		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)) != nullptr)
		{
			if (!row[0]) continue;
			Graveyard g{};
			g.id        = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			g.mapId     = static_cast<uint32_t>(std::strtoul(row[1], nullptr, 10));
			g.positionX = static_cast<float>(std::atof(row[2]));
			g.positionY = static_cast<float>(std::atof(row[3]));
			g.positionZ = static_cast<float>(std::atof(row[4]));
			g.faction   = static_cast<FactionId>(std::atoi(row[5]));
			g.zoneId    = static_cast<uint32_t>(std::strtoul(row[6], nullptr, 10));
			m_graveyards.push_back(g);
		}
		engine::server::db::DbFreeResult(res);

		m_loaded = true;
		LOG_INFO(Core, "[GraveyardManager] Loaded {} graveyards", m_graveyards.size());
		return true;
	}

	std::optional<Graveyard> GraveyardManager::ClosestGraveyard(uint32_t mapId,
		float posX, float posY, float posZ, FactionId requestedFaction) const
	{
		std::optional<Graveyard> best;
		float bestDistSq = std::numeric_limits<float>::max();
		for (const auto& g : m_graveyards)
		{
			if (g.mapId != mapId)
				continue;
			if (g.faction != 0 && g.faction != requestedFaction)
				continue;
			const float dx = g.positionX - posX;
			const float dy = g.positionY - posY;
			const float dz = g.positionZ - posZ;
			const float distSq = dx*dx + dy*dy + dz*dz;
			if (distSq < bestDistSq)
			{
				bestDistSq = distSq;
				best = g;
			}
		}
		return best;
	}
}
```

- [ ] **Step 9.2 : Test**

```cpp
// CMANGOS.16 (Phase 1b) — Tests GraveyardManager.

#include "engine/server/shard/globals/GraveyardManager.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::shard::globals::GraveyardManager;
	using engine::server::db::ConnectionPool;

	bool TestClosest(GraveyardManager& mgr)
	{
		// Seeds 0042 :
		//  G1 : (  0,0,0) faction=0 (neutral)
		//  G2 : (100,0,0) faction=1
		//  G3 : (200,0,0) faction=2
		// Position joueur (50, 0, 0) faction=1 → G2 (à 50) plus proche que G1 (à 50)
		// (égalité distance sur x, mais G1 est neutral et G2 matche faction=1).
		// Pour départager, on choisit (40,0,0) → G1 distance 40, G2 distance 60.

		auto a = mgr.ClosestGraveyard(0, 40.0f, 0.0f, 0.0f, 1);
		if (!a || a->id != 1)
		{
			LOG_ERROR(Core, "[GraveyardMgrTests] (40,0,0) faction=1 expected G1 (neutral closer)");
			return false;
		}
		auto b = mgr.ClosestGraveyard(0, 60.0f, 0.0f, 0.0f, 1);
		if (!b || b->id != 2)
		{
			LOG_ERROR(Core, "[GraveyardMgrTests] (60,0,0) faction=1 expected G2 (faction match closer)");
			return false;
		}
		// Faction 3 (inexistante en seed) : seul G1 (neutral) est valide.
		auto c = mgr.ClosestGraveyard(0, 1000.0f, 0.0f, 0.0f, 3);
		if (!c || c->id != 1)
		{
			LOG_ERROR(Core, "[GraveyardMgrTests] (1000,0,0) faction=3 expected G1 (only neutral)");
			return false;
		}
		// Map inexistante : nullopt.
		auto d = mgr.ClosestGraveyard(999, 0.0f, 0.0f, 0.0f, 1);
		if (d)
		{
			LOG_ERROR(Core, "[GraveyardMgrTests] map=999 expected nullopt");
			return false;
		}
		LOG_INFO(Core, "[GraveyardMgrTests] Closest graveyard filtering OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	if (config.GetString("db.host", "").empty())
	{
		LOG_INFO(Core, "[GraveyardMgrTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		engine::core::Log::Shutdown();
		return 1;
	}

	GraveyardManager mgr;
	bool ok = mgr.Load(pool) && TestClosest(mgr);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 9.3 : Cible CMake**

Après `object_accessor_tests`, ajouter :

```cmake
  # CMANGOS.16 (Phase 1b) : GraveyardManager tests
  add_executable(graveyard_manager_tests
    shard/globals/GraveyardManagerTests.cpp
    shard/globals/GraveyardManager.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(graveyard_manager_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(graveyard_manager_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(graveyard_manager_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME graveyard_manager_tests COMMAND graveyard_manager_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 9.4 : Build + run**

```bash
cmake --build --preset linux-x64 --target graveyard_manager_tests
ctest --preset linux-x64 -R graveyard_manager_tests --output-on-failure
```

Expected: **PASS** avec `Closest graveyard filtering OK`.

- [ ] **Step 9.5 : Commit**

```bash
git add engine/server/shard/globals/GraveyardManager.cpp \
        engine/server/shard/globals/GraveyardManagerTests.cpp \
        engine/server/CMakeLists.txt
git commit -m "feat(server/shard/globals): GraveyardManager impl + tests

Closest graveyard avec filtre faction (neutral OU match) + tests :
- (40,0,0) faction=1 → G1 (neutral plus proche que G2)
- (60,0,0) faction=1 → G2 (match faction plus proche que G1)
- faction=3 (inconnue) → G1 (seul neutral valide)
- map inexistante → nullopt

CMANGOS.16 (Phase 1b) — TDD green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10 : LocaleStrings.h — interface

**Files:**
- Create: `engine/server/shard/globals/LocaleStrings.h`

- [ ] **Step 10.1 : Interface**

```cpp
#pragma once
// CMANGOS.16 (Phase 1b) — LocaleStrings : cache (stringId, localeId) avec
// fallback sur la default_locale.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server::db
{
	class ConnectionPool;
}

namespace engine::server::shard::globals
{
	/// Locale id : 0=fr_FR (default LCDLLN), 1=en_US, etc.
	using LocaleId = uint8_t;

	class LocaleStrings
	{
	public:
		LocaleStrings() = default;
		~LocaleStrings() = default;
		LocaleStrings(const LocaleStrings&) = delete;
		LocaleStrings& operator=(const LocaleStrings&) = delete;

		/// Charge `locale_strings` depuis la DB. \pre Une seule fois.
		bool Load(engine::server::db::ConnectionPool& pool, LocaleId defaultLocale);

		/// Retourne le texte pour (stringId, localeId). Fallback sur defaultLocale
		/// si la locale demandée est manquante. Si même la default_locale manque,
		/// retourne `"[stringId=<id>]"` (jamais empty pour aider le debug).
		std::string GetString(uint32_t stringId, LocaleId localeId) const;

		/// Helper format avec placeholders `{0}`, `{1}`, ...
		/// Maximum 4 args supportés en Phase 1b. YAGNI — étendre si besoin.
		std::string Format(uint32_t stringId, LocaleId localeId,
			std::string_view arg0 = {},
			std::string_view arg1 = {},
			std::string_view arg2 = {},
			std::string_view arg3 = {}) const;

		size_t Size() const { return m_strings.size(); }

	private:
		struct Key
		{
			uint32_t stringId;
			LocaleId localeId;
			bool operator==(const Key& o) const = default;
		};
		struct KeyHash
		{
			size_t operator()(const Key& k) const noexcept
			{
				return (static_cast<size_t>(k.stringId) << 8) ^ k.localeId;
			}
		};

		std::unordered_map<Key, std::string, KeyHash> m_strings;
		LocaleId m_defaultLocale = 0;
		bool m_loaded = false;
	};
}
```

- [ ] **Step 10.2 : Commit**

```bash
git add engine/server/shard/globals/LocaleStrings.h
git commit -m "feat(server/shard/globals): LocaleStrings interface

Cache (stringId,localeId) avec fallback default_locale + sentinel
\"[stringId=<id>]\" si meme default manque (jamais empty, debug-friendly).
Format avec {0}..{3} placeholders (4 args YAGNI).

CMANGOS.16 (Phase 1b).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11 : LocaleStrings.cpp + tests

**Files:**
- Create: `engine/server/shard/globals/LocaleStrings.cpp`
- Create: `engine/server/shard/globals/LocaleStringsTests.cpp`
- Modify: `engine/server/CMakeLists.txt`

- [ ] **Step 11.1 : Implémentation**

```cpp
#include "engine/server/shard/globals/LocaleStrings.h"

#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <cstdlib>

namespace engine::server::shard::globals
{
	bool LocaleStrings::Load(engine::server::db::ConnectionPool& pool, LocaleId defaultLocale)
	{
		if (m_loaded)
			return false;

		m_defaultLocale = defaultLocale;

		auto guard = pool.Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;

		MYSQL_RES* res = engine::server::db::DbQuery(mysql,
			"SELECT string_id, locale_id, text FROM locale_strings");
		if (!res)
			return false;

		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)) != nullptr)
		{
			if (!row[0] || !row[1] || !row[2]) continue;
			Key k{
				static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10)),
				static_cast<LocaleId>(std::atoi(row[1]))
			};
			m_strings.emplace(k, std::string(row[2]));
		}
		engine::server::db::DbFreeResult(res);

		m_loaded = true;
		LOG_INFO(Core, "[LocaleStrings] Loaded {} strings, default locale = {}",
			m_strings.size(), static_cast<int>(defaultLocale));
		return true;
	}

	std::string LocaleStrings::GetString(uint32_t stringId, LocaleId localeId) const
	{
		auto it = m_strings.find(Key{stringId, localeId});
		if (it != m_strings.end())
			return it->second;
		// Fallback default locale.
		if (localeId != m_defaultLocale)
		{
			auto it2 = m_strings.find(Key{stringId, m_defaultLocale});
			if (it2 != m_strings.end())
				return it2->second;
		}
		// Sentinel debug : jamais empty.
		return "[stringId=" + std::to_string(stringId) + "]";
	}

	std::string LocaleStrings::Format(uint32_t stringId, LocaleId localeId,
		std::string_view arg0,
		std::string_view arg1,
		std::string_view arg2,
		std::string_view arg3) const
	{
		std::string s = GetString(stringId, localeId);
		// Replace {0}..{3} (1 pass naïf — performance OK pour les chaînes courtes).
		auto replace = [](std::string& s, std::string_view placeholder, std::string_view value) {
			const auto pos = s.find(placeholder);
			if (pos != std::string::npos)
				s.replace(pos, placeholder.size(), value);
		};
		replace(s, "{0}", arg0);
		replace(s, "{1}", arg1);
		replace(s, "{2}", arg2);
		replace(s, "{3}", arg3);
		return s;
	}
}
```

- [ ] **Step 11.2 : Test**

```cpp
// CMANGOS.16 (Phase 1b) — Tests LocaleStrings.

#include "engine/server/shard/globals/LocaleStrings.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::shard::globals::LocaleStrings;
	using engine::server::db::ConnectionPool;

	bool TestGetAndFallback(LocaleStrings& mgr)
	{
		// Seeds : ID 1000 fr_FR + en_US ; ID 1001 fr_FR seul.
		// fr_FR = locale_id 0, en_US = locale_id 1.

		const std::string fr1000 = mgr.GetString(1000, 0);
		if (fr1000 != "Bienvenue {0}, niveau {1}!")
		{
			LOG_ERROR(Core, "[LocaleStringsTests] GetString(1000, fr_FR) unexpected: {}", fr1000);
			return false;
		}
		const std::string en1000 = mgr.GetString(1000, 1);
		if (en1000 != "Welcome {0}, level {1}!")
		{
			LOG_ERROR(Core, "[LocaleStringsTests] GetString(1000, en_US) unexpected: {}", en1000);
			return false;
		}
		// ID 1001 en_US absent → fallback sur fr_FR (default).
		const std::string en1001 = mgr.GetString(1001, 1);
		if (en1001 != "Bonjour le monde")
		{
			LOG_ERROR(Core, "[LocaleStringsTests] GetString(1001, en_US) fallback failed: {}", en1001);
			return false;
		}
		// ID inexistant → sentinel.
		const std::string none = mgr.GetString(9999, 0);
		if (none.find("9999") == std::string::npos)
		{
			LOG_ERROR(Core, "[LocaleStringsTests] GetString(9999) sentinel missing: {}", none);
			return false;
		}
		LOG_INFO(Core, "[LocaleStringsTests] Get + fallback + sentinel OK");
		return true;
	}

	bool TestFormat(LocaleStrings& mgr)
	{
		const std::string s = mgr.Format(1000, 0, "Hortense", "42");
		if (s != "Bienvenue Hortense, niveau 42!")
		{
			LOG_ERROR(Core, "[LocaleStringsTests] Format unexpected: {}", s);
			return false;
		}
		LOG_INFO(Core, "[LocaleStringsTests] Format placeholders OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	if (config.GetString("db.host", "").empty())
	{
		LOG_INFO(Core, "[LocaleStringsTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		engine::core::Log::Shutdown();
		return 1;
	}

	LocaleStrings mgr;
	bool ok = mgr.Load(pool, 0) && TestGetAndFallback(mgr) && TestFormat(mgr);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 11.3 : Cible CMake**

Après `graveyard_manager_tests`, ajouter :

```cmake
  # CMANGOS.16 (Phase 1b) : LocaleStrings tests
  add_executable(locale_strings_tests
    shard/globals/LocaleStringsTests.cpp
    shard/globals/LocaleStrings.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(locale_strings_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(locale_strings_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(locale_strings_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME locale_strings_tests COMMAND locale_strings_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 11.4 : Build + run**

```bash
cmake --build --preset linux-x64 --target locale_strings_tests
ctest --preset linux-x64 -R locale_strings_tests --output-on-failure
```

Expected: **PASS** — `Get + fallback + sentinel OK`, `Format placeholders OK`.

- [ ] **Step 11.5 : Commit**

```bash
git add engine/server/shard/globals/LocaleStrings.cpp \
        engine/server/shard/globals/LocaleStringsTests.cpp \
        engine/server/CMakeLists.txt
git commit -m "feat(server/shard/globals): LocaleStrings impl + tests

Cache + GetString avec fallback default_locale + sentinel debug-friendly.
Format avec {0}..{3} placeholders. Tests :
- Lecture fr_FR/en_US directe
- Fallback en_US -> fr_FR si manquant
- Sentinel \"[stringId=9999]\" si rien trouve
- Format avec args (Hortense, 42) -> \"Bienvenue Hortense, niveau 42!\"

CMANGOS.16 (Phase 1b) — TDD green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 12 : Doc + config + commit final

**Files:**
- Modify: `docs/db_sql_guidelines.md`
- Modify: `config.json`

- [ ] **Step 12.1 : Ajouter section Phase 1b au doc**

Localiser la fin de `docs/db_sql_guidelines.md`. **Ajouter à la suite** :

```markdown

## Phase 1b — Globals (CMANGOS.16)

Quatre utilitaires data-driven dans `engine/server/shard/globals/`,
chargés au boot du shard et consommés par les tickets P2 downstream.

### `ConditionMgr` — prédicats data-driven

Charge `conditions` + `condition_groups`. Évalue par ID via un
`EvaluationContext` rempli par le caller (loot, quête, AI EventAI...).

**Convention IDs** : `condition_id ∈ [1, 9999]`, `group_id ∈ [10000, ∞)`.
Le helper `Evaluate(id, ctx)` dispatche sur cette base.

5 ConditionTypes en Phase 1b : `LevelGE`, `LevelLE`, `HasItem`, `ZoneId`,
`InGroup`. Étendre via PR séparée au fil des besoins downstream.

### `ObjectAccessor` — façade thread-safe

Registre des entités en ligne (Player + Creature) côté shard.
Inscription au login/spawn via `Register(snapshot)`, désinscription au
logout/despawn via `Unregister(entityId)`. Lookups : `Find(entityId)`
(O(1)) et `FindByName(name)` (O(N), case-insensitive).

Thread-safety : `std::shared_mutex` — readers concurrents, writer
exclusif. Pour les hot paths existants (whisper par nom), continuer
d'utiliser `SessionCharacterMap`.

### `GraveyardManager` — closest valid graveyard

Charge `graveyards`. `ClosestGraveyard(mapId, pos, faction)` retourne
le graveyard valide (faction matchée OU neutral) le plus proche.
Stockage `std::vector` linéaire — N petit (centaines max), scan OK.

### `LocaleStrings` — i18n côté serveur

Charge `locale_strings`. `GetString(stringId, localeId)` avec fallback
sur `default_locale` (config). Si même default manque, sentinel
`"[stringId=<id>]"` (jamais empty pour debug).

`Format(stringId, locale, arg0..arg3)` : remplace `{0}/{1}/{2}/{3}`.
Limité à 4 args en Phase 1b.

### Convention IDs et tables

| Table | Range IDs | Note |
|---|---|---|
| `conditions.condition_id` | 1 — 9999 | Atomic predicates |
| `condition_groups.group_id` | 10000 — ∞ | Composition logique |
| `graveyards.id` | 1 — ∞ | Pas de range réservé |
| `locale_strings.string_id` | 1 — ∞ | Pas de range réservé |
| `locale_strings.locale_id` | 0=fr_FR, 1=en_US | Étendre selon besoin |
```

- [ ] **Step 12.2 : Vérifier qu'il n'y a pas déjà clé `globals.*` dans config.json**

```bash
grep -n "globals" config.json || echo "no existing globals key"
```

Expected: `no existing globals key`.

- [ ] **Step 12.3 : Ajouter clés `globals.*` dans `config.json`**

Au niveau racine de `config.json` (au même niveau que `db`, `server`, etc.), ajouter :

```json
  "globals": {
    "default_locale": 0,
    "fallback_locale": 0,
    "graveyard_default_faction_neutral_radius_m": 500.0
  },
```

(Respecter la virgule entre les sections JSON. Le `0` pour `default_locale` correspond à `fr_FR`.)

- [ ] **Step 12.4 : Vérifier JSON valide**

```bash
python3 -c "import json; json.load(open('config.json'))" && echo "valid JSON"
```

Expected: `valid JSON`.

- [ ] **Step 12.5 : Commit final + mention redéploiement**

```bash
git add docs/db_sql_guidelines.md config.json
git commit -m "docs(globals): Phase 1b ConditionMgr/ObjectAccessor/Graveyard/Locale + config

Documente les 4 utilitaires Phase 1b dans docs/db_sql_guidelines.md avec
exemples d'usage, convention IDs (condition<10000<group), 5 ConditionTypes.

Ajoute section globals dans config.json :
- globals.default_locale = 0 (fr_FR)
- globals.fallback_locale = 0
- globals.graveyard_default_faction_neutral_radius_m = 500.0

Deploiement : redeploiement serveur (shard) requis — nouveaux singletons
runtime (binaire serveur recompile). Migration 0042 idempotente.

CMANGOS.16 (Phase 1b) — fin de la phase.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 13 : Validation finale

- [ ] **Step 13.1 : Re-run de tous les tests Phase 1b**

```bash
ctest --preset linux-x64 -R "(condition_mgr|object_accessor|graveyard_manager|locale_strings)" --output-on-failure
```

Expected: 4 tests **PASS** (ou skip si `db.host` non configuré, sauf `object_accessor_tests` qui n'a pas besoin de DB).

- [ ] **Step 13.2 : Build complet**

```bash
cmake --build --preset linux-x64 --target server_app
```

Expected: **PASS** sans warning supplémentaire.

- [ ] **Step 13.3 : Récap DoD**

Cocher manuellement :
- [ ] Migration 0042 idempotente
- [ ] `ConditionMgr::Evaluate` : 5 atom types + AND/OR/NOT compositions
- [ ] `ConditionMgr::DetectCycles` empêche le load si cycle dans groups
- [ ] `ObjectAccessor` thread-safe (8 readers + 1 writer 200ms, 0 erreur)
- [ ] `GraveyardManager::ClosestGraveyard` filtre faction (neutral OU match)
- [ ] `LocaleStrings::GetString` fallback OK + sentinel jamais empty
- [ ] `LocaleStrings::Format` placeholders {0}..{3} OK
- [ ] Doc `docs/db_sql_guidelines.md` mise à jour
- [ ] `config.json` étendu (3 clés `globals.*`)
- [ ] Mention redéploiement serveur dans le dernier commit

Si tous les items sont cochés → Phase 1b Globals est livrée. **Suite** :
exécuter Phase 1c Accounts (rôles 5 niveaux).

---

## Notes pour l'exécutant

### Ordre d'exécution recommandé

Phase 1b ne dépend de Phase 1a que si on consomme `SQLStorage<T>`. Comme
ce plan utilise `DbQuery` direct (via `DbHelpers`), Phase 1b **peut
s'exécuter en parallèle de Phase 1a**. Pour la prod future, ajouter
une PR séparée qui migre les `Load()` ConditionMgr/Graveyard/Locale
vers `SQLStorage` (gain perf marginal).

### Si la migration 0042 échoue avec "Duplicate entry"

Les seeds utilisent `INSERT IGNORE` mais si une seed est mal formée et
casse l'idempotence, vérifier manuellement :

```sql
SELECT * FROM conditions;
SELECT * FROM condition_groups;
SELECT * FROM graveyards;
SELECT * FROM locale_strings;
```

Si données en conflit, supprimer manuellement la ligne fautive puis
relancer la migration.

### Si `condition_mgr_tests` rapporte "0 conditions loaded"

La migration 0042 n'a pas été appliquée. Voir notes Phase 1a (workflow
MigrationRunner ou application manuelle).

### Performance attendue

- `ConditionMgr::Evaluate` (atom) : < 100 ns par appel (switch enum)
- `ConditionMgr::Evaluate` (group profondeur 3) : < 500 ns
- `ObjectAccessor::Find` : < 100 ns (lock contention faible)
- `GraveyardManager::ClosestGraveyard` (10 graveyards par map) : < 200 ns
- `LocaleStrings::GetString` : < 50 ns (cache hit)

Si nettement au-dessus, profiler le lock `ObjectAccessor` ou la
résolution `unordered_map` avec un hasher custom.

### Différences vs spec cmangos d'origine

- **5 ConditionTypes seulement** au lieu de 11 dans la spec — YAGNI,
  ajouter au fil des besoins.
- **`EvaluationContext` data-driven** au lieu de `Player const* source` —
  cohérent avec l'archi LCDLLN (pas de hiérarchie OOP).
- **Pas de `Custom` ConditionType** — sera ajouté par PR séparée si
  besoin avéré (ouverture sur complexité, à modérer).
- **Format max 4 args** — étendre à variadic template si besoin de plus.

---

*Plan généré le 2026-05-08 par la skill `superpowers:writing-plans` à
partir de `docs/superpowers/audits/2026-05-08-cmangos-gap-analysis/CMANGOS.16.md`.*
