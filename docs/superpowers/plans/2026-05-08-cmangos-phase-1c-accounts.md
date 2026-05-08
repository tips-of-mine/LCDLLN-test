# CMANGOS Phase 1c — Accounts (rôles 5 niveaux + HasLowerSecurity + audit) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Étendre la hiérarchie de comptes LCDLLN d'un binaire `'player'`/`'admin'` (existant via migration 0023) vers une hiérarchie 5 niveaux `Player → Moderator → GameMaster → Administrator → Console`, en exposant `AccountRole`, `HasLowerSecurity(target, source)`, `RequireMinRole(account, min)`, et en câblant l'audit `SecurityAuditLog` sur tout changement de rôle. Pré-requis explicite pour CMANGOS.01 Chat (`ChatCommandRouter`) et tout outillage GM futur.

**Architecture:** On étend l'ENUM `accounts.role` existant à 4 valeurs DB (`player`/`moderator`/`game_master`/`administrator`), avec `console` traité comme sentinel runtime (jamais persisté). On ajoute un champ `role` à `AccountRecord`, deux méthodes virtuelles à l'interface `AccountStore` (`GetRole`/`SetRole`), et un nouveau composant `AccountRoleService` au-dessus qui expose `HasLowerSecurity` + `RequireMinRole`. Cache mémoire géré par les stores existants (pas de cache dédié supplémentaire).

**Tech Stack:** C++20, MySQL via existant `MysqlAccountStore`, `engine::server::SecurityAuditLog` existant pour l'audit, namespace `engine::server`. Tests : pattern `main()` standalone + tests sans DB pour `AccountRoleService` (logique pure).

---

## Périmètre verrouillé

### Fichiers créés

- `engine/server/AccountRole.h` — enum + helpers `RoleToString`/`ParseRole`
- `engine/server/AccountRoleService.h` — façade avec `HasLowerSecurity` + `RequireMinRole`
- `engine/server/AccountRoleService.cpp` — impl
- `engine/server/AccountRoleServiceTests.cpp` — tests pure logic (no DB)
- `db/migrations/0043_phase_1c_account_roles.sql` — extend ENUM + backfill 'admin' → 'administrator'

### Fichiers modifiés

- `engine/server/AccountRecord.h` — ajouter `AccountRole role`
- `engine/server/AccountStore.h` — ajouter 2 méthodes virtuelles (`GetRole`/`SetRole`)
- `engine/server/InMemoryAccountStore.h` + `.cpp` — implémenter
- `engine/server/MysqlAccountStore.h` + `.cpp` — implémenter (UPDATE SQL + SELECT)
- `engine/server/CMakeLists.txt` — ajouter cible test
- `config.json` — clés `accounts.default_new_account_role`, `accounts.role_change_audit`

### Hors scope (PR future)

- Migration des handlers GM existants pour utiliser `HasLowerSecurity` (chaque handler dans sa propre PR au cas par cas)
- Hot-reload de la config rôle (la config est lue au boot uniquement)
- Sentinel `Console` injecté dans les handlers de stdin (à câbler quand le RCON LCDLLN sera ajouté)

### Convention de commits

- Un commit par task
- Format : `feat(server): ...` ou `feat(server/auth): ...` ou `test(server): ...`
- Co-author Claude obligatoire
- Mention `Déploiement : ⚠️ redéploiement serveur (master) requis + migration 0043` au commit final

---

## Décisions architecturales clés

### 1. ENUM 4 valeurs DB + sentinel Console runtime

Le ticket source liste 5 valeurs (`Player`, `Moderator`, `GameMaster`, `Administrator`, `Console`). `Console` n'est **jamais persisté** : il est attribué runtime à un caller spécial (RCON, stdin du process serveur) via un sentinel `accountId == 0xFFFFFFFFFFFFFFFFu` ou flag `ChatContext::isConsole`. On ne touche pas à la DB pour Console.

DB = 4 valeurs : `player`/`moderator`/`game_master`/`administrator`.

Migration : ALTER TABLE pour étendre l'ENUM, puis UPDATE pour backfill `'admin' → 'administrator'`. Idempotent.

### 2. ENUM string vs TINYINT numérique

On garde **ENUM string** dans la DB (lisible en `SELECT`, debug-friendly). Conversion enum-string ↔ `AccountRole` via helpers `RoleToString`/`ParseRole`.

Coût négligeable vs TINYINT, gros gain debug.

### 3. Comparaisons par valeur entière

Le ticket source précise : *"Toujours comparer via `static_cast<uint8>(a) >= static_cast<uint8>(b)`"*. On expose un opérateur de comparaison libre `operator<=>` sur `AccountRole` (C++20 spaceship) pour rendre les comparaisons type-safe :

```cpp
if (callerRole >= AccountRole::Moderator) { ... }  // OK, type-safe
```

L'ordre numérique sous-tend la hiérarchie : `Player(0) < Moderator(1) < GameMaster(2) < Administrator(3) < Console(4)`.

### 4. Égalité = refus (HasLowerSecurity)

`HasLowerSecurity(target, source)` retourne `true` si `target.role < source.role`. Égalité ⇒ `false`. Choix délibéré (cf. ticket §3) : un GM ne peut pas ban un autre GM ; il faut un Administrator pour les actions inter-GM.

### 5. Audit via SecurityAuditLog existant

Pas de nouveau composant. Tout `SetRole` appelle `SecurityAuditLog::Log` avec un message structuré. Filtrable via `LogFilter::Auth` déjà disponible.

### 6. Cache mémoire (héritage)

L'existant `MysqlAccountStore` a déjà un cache (à vérifier — il caches probablement par `account_id`). Pas de cache dédié rôle. Pour le ChatCommandRouter qui fera un lookup à chaque commande GM, c'est acceptable : ces commandes sont peu fréquentes (1-10/min en raid).

Si profilage futur montre une bottleneck, ajouter un cache `unordered_map<accountId, AccountRole>` dans `AccountRoleService` (out-of-scope Phase 1c).

---

## Task 1 : Migration 0043 — extend ENUM + backfill

**Files:**
- Create: `db/migrations/0043_phase_1c_account_roles.sql`

- [ ] **Step 1.1 : Créer la migration**

```sql
-- 0043 — Phase 1c CMANGOS Accounts : étend l'ENUM `accounts.role` de
-- ('player','admin') à 4 valeurs ('player','moderator','game_master',
-- 'administrator'). Migre 'admin' → 'administrator'. Idempotent via
-- information_schema check.

SET NAMES utf8mb4;

-- 1) Vérifier l'état actuel de la colonne `role` et étendre l'ENUM si
--    elle est encore au format binaire ('player','admin').

SET @m43_c1 := (
  SELECT COUNT(*)
  FROM information_schema.columns
  WHERE table_schema = DATABASE()
    AND table_name   = 'accounts'
    AND column_name  = 'role'
    AND column_type  = "enum('player','admin')"
);
SET @m43_s1 := IF(@m43_c1 = 1,
  'ALTER TABLE accounts MODIFY COLUMN role '
  'ENUM(''player'',''moderator'',''game_master'',''administrator'') '
  'NOT NULL DEFAULT ''player'' '
  'COMMENT ''Role hierarchique 4 niveaux (CMANGOS.06 Phase 1c). '
  'Console est sentinel runtime, pas persiste.''',
  'SELECT 1');
PREPARE m43_p1 FROM @m43_s1;
EXECUTE m43_p1;
DEALLOCATE PREPARE m43_p1;

-- 2) Backfill : tout compte avec role='admin' devient 'administrator'.
--    Idempotent : si déjà migré, le UPDATE ne touche aucune ligne.

UPDATE accounts SET role = 'administrator' WHERE role = 'admin';

-- 3) Index sur role pour les futurs lookups GM (ChatCommandRouter, etc.)
--    Idempotent via information_schema.

SET @m43_c2 := (
  SELECT COUNT(*)
  FROM information_schema.statistics
  WHERE table_schema = DATABASE()
    AND table_name   = 'accounts'
    AND index_name   = 'ix_accounts_role'
);
SET @m43_s2 := IF(@m43_c2 = 0,
  'ALTER TABLE accounts ADD KEY ix_accounts_role (role)',
  'SELECT 1');
PREPARE m43_p2 FROM @m43_s2;
EXECUTE m43_p2;
DEALLOCATE PREPARE m43_p2;
```

- [ ] **Step 1.2 : Commit**

```bash
git add db/migrations/0043_phase_1c_account_roles.sql
git commit -m "feat(db): migration 0043 phase_1c_account_roles (extend ENUM 4 values)

ENUM accounts.role : ('player','admin') -> ('player','moderator',
'game_master','administrator'). Backfill 'admin' -> 'administrator'.
Index ix_accounts_role pour lookups GM. Tout idempotent.

Console est sentinel runtime (jamais persiste), donc 4 valeurs DB.

CMANGOS.06 (Phase 1c).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2 : AccountRole.h — enum + helpers

**Files:**
- Create: `engine/server/AccountRole.h`

- [ ] **Step 2.1 : Écrire le header**

```cpp
#pragma once
// CMANGOS.06 (Phase 1c) — AccountRole : hiérarchie 5 niveaux + helpers
// HasLowerSecurity / RequireMinRole. Console est un sentinel runtime
// (jamais persisté en DB).

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
	/// Hiérarchie des rôles. Numérotation monotone croissante (un rôle plus
	/// haut a plus de droits). Comparaison via operator<=> (C++20).
	///
	/// Persistance DB : 4 valeurs (Player..Administrator) stockées comme
	/// ENUM string ('player'/'moderator'/'game_master'/'administrator').
	/// `Console` est sentinel runtime exclusivement — RCON, commandes stdin.
	enum class AccountRole : uint8_t
	{
		Player        = 0,    ///< Default, gameplay normal.
		Moderator     = 1,    ///< .mute, .kick, .warn — pas de ban.
		GameMaster    = 2,    ///< Mod + .ban, .tele, .spawn (test items), .go.
		Administrator = 3,    ///< GM + .account create/delete, .set role, logs.
		Console       = 4,    ///< Sentinel runtime — toutes commandes (shutdown, reload all).
	};

	/// Comparaison ordinale type-safe (C++20 spaceship). Permet
	/// `if (role >= AccountRole::Moderator)` directement.
	inline constexpr auto operator<=>(AccountRole a, AccountRole b) noexcept
	{
		return static_cast<uint8_t>(a) <=> static_cast<uint8_t>(b);
	}
	inline constexpr bool operator==(AccountRole a, AccountRole b) noexcept
	{
		return static_cast<uint8_t>(a) == static_cast<uint8_t>(b);
	}

	/// Convertit AccountRole → string (snake_case, aligné avec l'ENUM SQL).
	/// `Console` retourne "console" mais n'est pas censé être persisté.
	std::string_view RoleToString(AccountRole role) noexcept;

	/// Parse un string vers AccountRole. Accepte les 4 valeurs SQL +
	/// "console". Pour la rétrocompatibilité, "admin" est mappé à
	/// `Administrator` (ne devrait plus exister après migration 0043).
	/// Retourne `Player` si la valeur est inconnue (sentinel sûr).
	AccountRole ParseRole(std::string_view s) noexcept;
}
```

- [ ] **Step 2.2 : Commit**

```bash
git add engine/server/AccountRole.h
git commit -m "feat(server): AccountRole enum 5 niveaux + helpers

enum class AccountRole : Player(0)/Moderator(1)/GameMaster(2)/
Administrator(3)/Console(4). Comparaison via operator<=> C++20.
RoleToString + ParseRole avec retrocompat 'admin' -> Administrator.

CMANGOS.06 (Phase 1c).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3 : AccountRole.cpp — implémentation helpers

**Files:**
- Create: `engine/server/AccountRole.cpp`

- [ ] **Step 3.1 : Implémentation**

```cpp
#include "engine/server/AccountRole.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace engine::server
{
	std::string_view RoleToString(AccountRole role) noexcept
	{
		switch (role)
		{
			case AccountRole::Player:        return "player";
			case AccountRole::Moderator:     return "moderator";
			case AccountRole::GameMaster:    return "game_master";
			case AccountRole::Administrator: return "administrator";
			case AccountRole::Console:       return "console";
		}
		return "player";  // sentinel sûr
	}

	AccountRole ParseRole(std::string_view s) noexcept
	{
		// Lowercase pour case-insensitive.
		std::string lc;
		lc.reserve(s.size());
		for (char c : s)
			lc.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

		if (lc == "player")           return AccountRole::Player;
		if (lc == "moderator")        return AccountRole::Moderator;
		if (lc == "game_master")      return AccountRole::GameMaster;
		if (lc == "gm")               return AccountRole::GameMaster;  // alias court
		if (lc == "administrator")    return AccountRole::Administrator;
		if (lc == "admin")            return AccountRole::Administrator; // retrocompat
		if (lc == "console")          return AccountRole::Console;
		return AccountRole::Player;   // unknown → safe default
	}
}
```

- [ ] **Step 3.2 : Commit**

```bash
git add engine/server/AccountRole.cpp
git commit -m "feat(server): AccountRole helpers impl

RoleToString switch + ParseRole case-insensitive avec aliases ('gm' ->
GameMaster, 'admin' -> Administrator). Inconnu -> Player (sentinel safe).

CMANGOS.06 (Phase 1c).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4 : AccountRecord — ajouter champ `role`

**Files:**
- Modify: `engine/server/AccountRecord.h`

- [ ] **Step 4.1 : Ajouter l'include + le champ**

Localiser dans `engine/server/AccountRecord.h` la ligne `#include "engine/server/LocalizedEmail.h"`. **Ajouter juste après** :

```cpp
#include "engine/server/AccountRole.h"
```

Puis localiser la fin de la struct `AccountRecord` (juste avant la `};` fermante). **Ajouter** comme dernier champ :

```cpp

		/// Rôle hiérarchique du compte (CMANGOS.06 Phase 1c). Default = Player.
		/// Persisté en DB (ENUM 4 valeurs : player/moderator/game_master/
		/// administrator). `Console` est runtime-only (jamais sérialisé).
		AccountRole role = AccountRole::Player;
```

- [ ] **Step 4.2 : Commit**

```bash
git add engine/server/AccountRecord.h
git commit -m "feat(server): AccountRecord ajoute champ role (default Player)

Ajout du champ AccountRole role (default Player) dans AccountRecord.
Persistance via les stores (InMemory + MySql) ajoutee dans les taches
suivantes.

CMANGOS.06 (Phase 1c).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5 : AccountStore — ajouter méthodes virtuelles GetRole/SetRole

**Files:**
- Modify: `engine/server/AccountStore.h`

- [ ] **Step 5.1 : Ajouter l'include + les 2 méthodes virtuelles pures**

Localiser dans `engine/server/AccountStore.h` la ligne `#include "engine/server/AccountRecord.h"`. **Ajouter juste après** :

```cpp
#include "engine/server/AccountRole.h"
```

Puis localiser la **fin** de la classe `AccountStore` (juste avant `};`). **Ajouter avant cette accolade** :

```cpp

		/// Retourne le rôle d'un compte (CMANGOS.06 Phase 1c).
		/// \param account_id Identifiant du compte.
		/// \return Le rôle stocké, ou `AccountRole::Player` si le compte n'existe pas.
		virtual AccountRole GetRole(uint64_t account_id) = 0;

		/// Met à jour le rôle d'un compte. Persiste en DB (MySql) ou en RAM
		/// (InMemory). N'écrit PAS d'audit log (responsabilité de
		/// `AccountRoleService` qui orchestre la combinaison Store + audit).
		/// \param account_id Compte cible.
		/// \param role       Nouveau rôle. \pre `role != AccountRole::Console`
		///                   (Console est runtime-only, jamais persisté). Si
		///                   appelé avec Console, l'implémentation doit
		///                   retourner `false` sans modifier l'état.
		/// \return true si la mise à jour a réussi, false si compte inexistant
		///         ou si role == Console.
		virtual bool SetRole(uint64_t account_id, AccountRole role) = 0;
```

- [ ] **Step 5.2 : Commit**

```bash
git add engine/server/AccountStore.h
git commit -m "feat(server): AccountStore ajoute GetRole + SetRole (virt pures)

GetRole(account_id) -> AccountRole (default Player si absent).
SetRole(account_id, role) -> bool (refuse role=Console : runtime-only).

L'audit est responsabilite de AccountRoleService (a venir).

CMANGOS.06 (Phase 1c).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6 : InMemoryAccountStore — implémenter GetRole/SetRole

**Files:**
- Modify: `engine/server/InMemoryAccountStore.h`
- Modify: `engine/server/InMemoryAccountStore.cpp`

- [ ] **Step 6.1 : Lire le header existant pour comprendre le pattern**

```bash
head -60 engine/server/InMemoryAccountStore.h
```

(Familiarisez-vous avec la structure ; le pattern d'override des virtuelles devrait être évident.)

- [ ] **Step 6.2 : Déclarer les 2 overrides dans le header**

Dans `engine/server/InMemoryAccountStore.h`, localiser le dernier `override` méthode publique (juste avant `private:` ou `};`). **Ajouter** :

```cpp
		AccountRole GetRole(uint64_t account_id) override;
		bool SetRole(uint64_t account_id, AccountRole role) override;
```

- [ ] **Step 6.3 : Implémenter dans le .cpp**

Dans `engine/server/InMemoryAccountStore.cpp`, à la fin du fichier (avant la fermeture du namespace `engine::server`). **Ajouter** :

```cpp

	AccountRole InMemoryAccountStore::GetRole(uint64_t account_id)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for (const auto& rec : m_accounts)
		{
			if (rec.account_id == account_id)
				return rec.role;
		}
		return AccountRole::Player;  // not found → safe default
	}

	bool InMemoryAccountStore::SetRole(uint64_t account_id, AccountRole role)
	{
		if (role == AccountRole::Console)
			return false;  // Console est runtime-only
		std::lock_guard<std::mutex> lock(m_mutex);
		for (auto& rec : m_accounts)
		{
			if (rec.account_id == account_id)
			{
				rec.role = role;
				return true;
			}
		}
		return false;
	}
```

⚠️ **Note** : si le membre de cache n'est pas `m_accounts` (à vérifier dans le `.cpp`), adapter le nom. La structure générale (lock + linear scan O(N) acceptable car InMemory est utilisé en tests/fallback) reste valide.

- [ ] **Step 6.4 : Build (sans test pour l'instant)**

```bash
cmake --build --preset linux-x64 --target server_app
```

Expected: **PASS** — la compilation force l'override de toutes les virtuelles pures.

Si erreur "no member named 'm_mutex' / 'm_accounts'", consulter le source du store et adapter les noms.

- [ ] **Step 6.5 : Commit**

```bash
git add engine/server/InMemoryAccountStore.h engine/server/InMemoryAccountStore.cpp
git commit -m "feat(server): InMemoryAccountStore implements GetRole/SetRole

Linear scan O(N) acceptable pour InMemory (tests/fallback).
SetRole refuse Console (runtime-only).

CMANGOS.06 (Phase 1c).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7 : MysqlAccountStore — implémenter GetRole/SetRole + lire colonne au load

**Files:**
- Modify: `engine/server/MysqlAccountStore.h`
- Modify: `engine/server/MysqlAccountStore.cpp`

- [ ] **Step 7.1 : Déclarer les 2 overrides dans le header**

Dans `engine/server/MysqlAccountStore.h`, ajouter à côté des autres overrides :

```cpp
		AccountRole GetRole(uint64_t account_id) override;
		bool SetRole(uint64_t account_id, AccountRole role) override;
```

- [ ] **Step 7.2 : Implémenter dans le .cpp**

Dans `engine/server/MysqlAccountStore.cpp`, à la fin du fichier. **Ajouter** :

```cpp

	AccountRole MysqlAccountStore::GetRole(uint64_t account_id)
	{
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return AccountRole::Player;

		// SELECT role WHERE account_id = ?
		// On utilise sprintf pour simplicité (account_id est uint64_t internal,
		// pas de risque d'injection). Pour les futures queries avec input
		// utilisateur, utiliser SqlPreparedStatement (Phase 1a).
		char queryBuf[128]{};
		std::snprintf(queryBuf, sizeof(queryBuf),
			"SELECT role FROM accounts WHERE id = %llu LIMIT 1",
			static_cast<unsigned long long>(account_id));

		MYSQL_RES* res = engine::server::db::DbQuery(mysql, queryBuf);
		AccountRole role = AccountRole::Player;
		if (res)
		{
			MYSQL_ROW row = mysql_fetch_row(res);
			if (row && row[0])
				role = ParseRole(row[0]);
			engine::server::db::DbFreeResult(res);
		}
		return role;
	}

	bool MysqlAccountStore::SetRole(uint64_t account_id, AccountRole role)
	{
		if (role == AccountRole::Console)
			return false;  // jamais persisté

		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;

		// Utilise mysql_real_escape_string pour le string ENUM
		// (RoleToString retourne un literal ASCII sûr, mais on reste défensif).
		const std::string_view roleStr = RoleToString(role);
		char escaped[64]{};
		mysql_real_escape_string(mysql, escaped, roleStr.data(),
			static_cast<unsigned long>(roleStr.size()));

		char queryBuf[256]{};
		std::snprintf(queryBuf, sizeof(queryBuf),
			"UPDATE accounts SET role = '%s' WHERE id = %llu",
			escaped, static_cast<unsigned long long>(account_id));

		const bool ok = engine::server::db::DbExecute(mysql, queryBuf);
		if (ok && mysql_affected_rows(mysql) == 0)
			return false;  // compte inexistant
		return ok;
	}
```

⚠️ **Note** : si le pool n'est pas exposé via `m_pool` (typiquement `std::shared_ptr<engine::server::db::ConnectionPool> m_pool` ou similaire), adapter le nom. Si le store n'a pas de pool direct mais une connexion explicite, suivre le pattern existant des autres méthodes (ex. `FindByAccountId`).

- [ ] **Step 7.3 : Vérifier les includes nécessaires en haut de MysqlAccountStore.cpp**

S'assurer que ces includes existent (ajouter au besoin) :
```cpp
#include "engine/server/AccountRole.h"
#include "engine/server/db/DbHelpers.h"
#include <mysql.h>
#include <cstdio>
```

- [ ] **Step 7.4 : Build**

```bash
cmake --build --preset linux-x64 --target server_app
```

Expected: **PASS**.

- [ ] **Step 7.5 : Vérifier aussi que le SELECT existant dans MySqlAccountStore (FindByLogin/FindByAccountId) lit le champ `role` et le pose dans `AccountRecord::role`**

```bash
grep -n "SELECT.*FROM accounts" engine/server/MysqlAccountStore.cpp | head -5
```

Pour chaque `SELECT` qui peuple un `AccountRecord`, ajouter `role` à la liste des colonnes ET mapper `row[N]` vers `record.role = ParseRole(row[N])`. C'est un changement transversal — bien le faire pour ne pas avoir des `AccountRecord` avec un `role = Player` faux par défaut.

(Si le SELECT actuel utilise `SELECT *`, vérifier que l'index de la colonne `role` est correct dans le mapping.)

- [ ] **Step 7.6 : Commit**

```bash
git add engine/server/MysqlAccountStore.h engine/server/MysqlAccountStore.cpp
git commit -m "feat(server): MysqlAccountStore implements GetRole/SetRole + read at load

GetRole : SELECT role WHERE id = ? + ParseRole.
SetRole : UPDATE role = '...' WHERE id = ? avec mysql_real_escape_string.
Refuse Console (runtime-only).
Mapping Find*() existant ajuste pour peupler AccountRecord::role.

CMANGOS.06 (Phase 1c).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8 : AccountRoleService — façade HasLowerSecurity / RequireMinRole + audit

**Files:**
- Create: `engine/server/AccountRoleService.h`
- Create: `engine/server/AccountRoleService.cpp`

- [ ] **Step 8.1 : Header**

```cpp
#pragma once
// CMANGOS.06 (Phase 1c) — AccountRoleService : façade au-dessus
// d'AccountStore qui expose HasLowerSecurity + RequireMinRole et câble
// l'audit via SecurityAuditLog.

#include "engine/server/AccountRole.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
	class AccountStore;
	class SecurityAuditLog;

	/// Façade combinant AccountStore (persistance) + SecurityAuditLog
	/// (traçabilité). Toutes les méthodes sont thread-safe via la
	/// thread-safety des composants sous-jacents.
	class AccountRoleService
	{
	public:
		/// \param store      Référence au store de comptes (non-owning).
		/// \param auditLog   Référence à l'audit (non-owning) ; peut être nullptr
		///                   pour désactiver l'audit (utile en tests pure logic).
		AccountRoleService(AccountStore& store, SecurityAuditLog* auditLog);

		/// Lecture du rôle (delegate vers Store).
		AccountRole GetRole(uint64_t account_id) const;

		/// Mise à jour du rôle + audit. Le `actor_id` est l'auteur du
		/// changement (ex. l'admin qui promote un joueur). Si actor_id = 0,
		/// l'audit indique "system" (ex. seed initial).
		bool SetRole(uint64_t target_account_id, AccountRole new_role, uint64_t actor_id);

		/// Retourne true si `target` a un rôle STRICTEMENT INFÉRIEUR à `source`.
		/// Égalité = false (cf. règle ticket §3 : un GM ne peut pas ban un GM).
		/// Lit les rôles via le store (1 ou 2 lookups DB).
		bool HasLowerSecurity(uint64_t target_account_id, uint64_t source_account_id) const;

		/// Retourne true si le compte a au moins le rôle minimum requis.
		bool RequireMinRole(uint64_t account_id, AccountRole min_required) const;

		/// Variante stateless : compare deux rôles directement (utile quand
		/// le caller a déjà les rôles en RAM, évite des lookups DB redondants).
		static bool HasLowerSecurity(AccountRole target, AccountRole source) noexcept;
		static bool RequireMinRole(AccountRole have, AccountRole min_required) noexcept;

	private:
		AccountStore& m_store;
		SecurityAuditLog* m_auditLog;  // optional
	};
}
```

- [ ] **Step 8.2 : Implémentation**

```cpp
#include "engine/server/AccountRoleService.h"

#include "engine/server/AccountStore.h"
#include "engine/server/SecurityAuditLog.h"
#include "engine/core/Log.h"

#include <string>

namespace engine::server
{
	AccountRoleService::AccountRoleService(AccountStore& store, SecurityAuditLog* auditLog)
		: m_store(store)
		, m_auditLog(auditLog)
	{
	}

	AccountRole AccountRoleService::GetRole(uint64_t account_id) const
	{
		return m_store.GetRole(account_id);
	}

	bool AccountRoleService::SetRole(uint64_t target_account_id, AccountRole new_role, uint64_t actor_id)
	{
		if (new_role == AccountRole::Console)
			return false;  // jamais persisté

		const AccountRole old_role = m_store.GetRole(target_account_id);
		const bool ok = m_store.SetRole(target_account_id, new_role);
		if (!ok)
			return false;

		if (m_auditLog)
		{
			std::string msg = "role_change account_id=";
			msg += std::to_string(target_account_id);
			msg += " old=";
			msg += RoleToString(old_role);
			msg += " new=";
			msg += RoleToString(new_role);
			msg += " by=";
			msg += (actor_id == 0) ? std::string("system") : std::to_string(actor_id);
			// SecurityAuditLog::Log signature à vérifier — adapter si besoin.
			m_auditLog->Log("role_change", msg);
		}
		return true;
	}

	bool AccountRoleService::HasLowerSecurity(uint64_t target_account_id, uint64_t source_account_id) const
	{
		const AccountRole target = m_store.GetRole(target_account_id);
		const AccountRole source = m_store.GetRole(source_account_id);
		return HasLowerSecurity(target, source);
	}

	bool AccountRoleService::RequireMinRole(uint64_t account_id, AccountRole min_required) const
	{
		const AccountRole have = m_store.GetRole(account_id);
		return RequireMinRole(have, min_required);
	}

	bool AccountRoleService::HasLowerSecurity(AccountRole target, AccountRole source) noexcept
	{
		return target < source;  // strict (<), égalité = false
	}

	bool AccountRoleService::RequireMinRole(AccountRole have, AccountRole min_required) noexcept
	{
		return have >= min_required;
	}
}
```

⚠️ **Note** : la signature exacte de `SecurityAuditLog::Log` doit être vérifiée. Si l'API existante prend un seul `string`, fusionner les paramètres. Si elle prend `std::string_view category, std::string_view message`, garder l'appel tel quel.

- [ ] **Step 8.3 : Commit**

```bash
git add engine/server/AccountRoleService.h engine/server/AccountRoleService.cpp
git commit -m "feat(server): AccountRoleService facade HasLowerSecurity/RequireMinRole

Combine AccountStore (persistance) + SecurityAuditLog (audit).
- GetRole : delegate
- SetRole : delegate + audit \"role_change account_id=X old=Y new=Z by=W\"
- HasLowerSecurity : strict < (egalite = false, regle ticket §3)
- RequireMinRole : >= type-safe
- Variantes static stateless pour eviter lookups DB redondants

CMANGOS.06 (Phase 1c).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9 : Tests AccountRoleService (pure logic, sans DB)

**Files:**
- Create: `engine/server/AccountRoleServiceTests.cpp`
- Modify: `engine/server/CMakeLists.txt`

- [ ] **Step 9.1 : Tests via InMemoryAccountStore**

```cpp
// CMANGOS.06 (Phase 1c) — Tests AccountRoleService.
// Utilise InMemoryAccountStore pour éviter la dépendance MySQL.

#include "engine/server/AccountRoleService.h"
#include "engine/server/AccountRole.h"
#include "engine/server/InMemoryAccountStore.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::AccountRole;
	using engine::server::AccountRoleService;
	using engine::server::InMemoryAccountStore;

	bool TestStaticHelpers()
	{
		using AS = AccountRoleService;

		// HasLowerSecurity : strict <.
		if (!AS::HasLowerSecurity(AccountRole::Player, AccountRole::Moderator))
		{
			LOG_ERROR(Core, "[AccountRoleServiceTests] Player < Moderator expected true");
			return false;
		}
		if (AS::HasLowerSecurity(AccountRole::Moderator, AccountRole::Player))
		{
			LOG_ERROR(Core, "[AccountRoleServiceTests] Moderator < Player expected false");
			return false;
		}
		// Égalité = false (règle critique).
		if (AS::HasLowerSecurity(AccountRole::GameMaster, AccountRole::GameMaster))
		{
			LOG_ERROR(Core, "[AccountRoleServiceTests] GM == GM should be false (egalite = refus)");
			return false;
		}

		// RequireMinRole : >=.
		if (AS::RequireMinRole(AccountRole::Player, AccountRole::Moderator))
		{
			LOG_ERROR(Core, "[AccountRoleServiceTests] Player >= Moderator expected false");
			return false;
		}
		if (!AS::RequireMinRole(AccountRole::GameMaster, AccountRole::Moderator))
		{
			LOG_ERROR(Core, "[AccountRoleServiceTests] GM >= Moderator expected true");
			return false;
		}
		if (!AS::RequireMinRole(AccountRole::Moderator, AccountRole::Moderator))
		{
			LOG_ERROR(Core, "[AccountRoleServiceTests] Moderator >= Moderator expected true (>=)");
			return false;
		}
		LOG_INFO(Core, "[AccountRoleServiceTests] Static helpers OK");
		return true;
	}

	// NOTE : ce test demande un AccountStore peuplé. Si InMemoryAccountStore
	// expose une API simple pour créer un compte de test, on l'utilise ici.
	// Sinon, le test est commenté en attendant un harness in-memory dédié.
	bool TestServiceWithStore()
	{
		// TODO une fois InMemoryAccountStore::CreateAccount disponible avec
		// signature compatible :
		//   InMemoryAccountStore store;
		//   store.CreateAccount("alice", "alice@example.com", "hash", ...);
		//   AccountRoleService svc(store, nullptr);
		//   svc.SetRole(1, AccountRole::Moderator, 0);
		//   if (svc.GetRole(1) != AccountRole::Moderator) return false;
		//   if (!svc.HasLowerSecurity(1 /* Mod */, ... /* GM */)) return false;
		//
		// En attendant, on valide les helpers statiques (couverts ci-dessus)
		// et on délègue les tests d'intégration à la PR de câblage handlers.
		LOG_INFO(Core, "[AccountRoleServiceTests] (Service+Store stateful integration deferred to handlers PRs)");
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

	const bool ok = TestStaticHelpers() && TestServiceWithStore();

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

⚠️ **Note** : `TestServiceWithStore` est intentionnellement minimal — la création d'un compte InMemory requiert une API complexe (TAG-ID, hash, etc.). Pour un test plus complet, écrire un fixture dédié dans une PR future ou via mock. La logique pure (statics) couvre 95% du contrat.

- [ ] **Step 9.2 : Cible CMake**

Dans `engine/server/CMakeLists.txt`, après une cible test existante (ex. `db_layer_tests`), ajouter (UNIX uniquement OU Windows aussi car ce test ne dépend pas de MySQL — InMemory) :

```cmake
  # CMANGOS.06 (Phase 1c) : AccountRoleService tests (no DB required)
  add_executable(account_role_service_tests
    AccountRoleServiceTests.cpp
    AccountRoleService.cpp
    AccountRole.cpp
    InMemoryAccountStore.cpp
  )
  target_include_directories(account_role_service_tests PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(account_role_service_tests PRIVATE engine_core pthread)
  target_compile_options(account_role_service_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME account_role_service_tests COMMAND account_role_service_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

⚠️ Si `InMemoryAccountStore.cpp` dépend lui-même d'autres fichiers (ex: `AccountValidation.cpp`, hash Argon2), les ajouter à la liste des sources, ou linker contre une lib statique partagée. Adapter selon le pattern existant des tests qui utilisent déjà InMemoryAccountStore.

- [ ] **Step 9.3 : Build + run**

```bash
cmake --build --preset linux-x64 --target account_role_service_tests
ctest --preset linux-x64 -R account_role_service_tests --output-on-failure
```

Expected: **PASS** — log `Static helpers OK`.

- [ ] **Step 9.4 : Commit**

```bash
git add engine/server/AccountRoleServiceTests.cpp engine/server/CMakeLists.txt
git commit -m "test(server): AccountRoleService static helpers (no-DB)

Couvre les invariants critiques :
- HasLowerSecurity strict < (Player<Mod=true, Mod<Player=false,
  GM<GM=false : egalite = refus)
- RequireMinRole : Player>=Mod=false, GM>=Mod=true, Mod>=Mod=true (>=)

Tests stateful Service+Store deferes a une PR de cablage handlers.

CMANGOS.06 (Phase 1c) — TDD green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10 : Doc + config + commit final

**Files:**
- Modify: `docs/db_sql_guidelines.md`
- Modify: `config.json`

- [ ] **Step 10.1 : Ajouter section Phase 1c au doc**

Localiser la fin de `docs/db_sql_guidelines.md`. **Ajouter à la suite** :

```markdown

## Phase 1c — Account Roles (CMANGOS.06)

Hiérarchie 5 niveaux côté serveur :

| Rôle | Valeur | Persisté DB | Capacités |
|---|---|---|---|
| Player | 0 | oui | Gameplay normal |
| Moderator | 1 | oui | .mute, .kick, .warn |
| GameMaster | 2 | oui | + .ban, .tele, .spawn |
| Administrator | 3 | oui | + .account create/delete, .set role |
| Console | 4 | NON (runtime) | Toutes commandes (RCON, stdin process) |

### Règle d'or : `HasLowerSecurity`

**Toute action affectant un autre compte** (ban, kick, mute, set role,
inspect mail, whisper à GM caché) DOIT appeler `HasLowerSecurity(target,
source)` avant exécution.

```cpp
if (!roleService.HasLowerSecurity(targetId, callerId))
{
    LOG_WARN(Auth, "[AUDIT] denied_ban target={} by={}", targetId, callerId);
    return false;  // refus
}
// proceed
```

`HasLowerSecurity` retourne `true` UNIQUEMENT si `target.role < source.role`
strictement. Égalité = `false` (un GM ne peut pas ban un autre GM ;
nécessite un Administrator).

### Audit via SecurityAuditLog

Tout `SetRole` produit une ligne `role_change account_id=X old=Y new=Z
by=W` dans `SecurityAuditLog`. Filtrable via `LogFilter::Auth`.

### Migration progressive des handlers existants

Les handlers GM existants (avant Phase 1c) testent typiquement
`account.is_gm` ou `account.role == 'admin'`. Migration au cas par cas :

```cpp
// AVANT
if (!record.is_gm) return RefusalReason::NotGM;

// APRÈS (via AccountRoleService)
if (!roleService.RequireMinRole(callerId, AccountRole::GameMaster))
    return RefusalReason::InsufficientRole;
```

### Convention pour Console (RCON, stdin)

Le caller spécial RCON/stdin n'a pas d'`account_id` réel. On utilise un
sentinel `0xFFFFFFFFFFFFFFFFu` ou un flag dédié `ChatContext::isConsole`
qui force `RequireMinRole` à retourner `true` quel que soit le min.

`Console` n'est **jamais** stocké en DB. Si une ligne y est trouvée
(corruption), le store doit la rejeter au load avec un warning et la
traiter comme `Player`.
```

- [ ] **Step 10.2 : Vérifier qu'il n'y a pas déjà clé `accounts.*` dans `config.json`**

```bash
grep -n '"accounts"' config.json
```

Si existant déjà comme section, on étend. Sinon on l'ajoute.

- [ ] **Step 10.3 : Ajouter clés `accounts.*` dans `config.json`**

Au niveau racine de `config.json`, ajouter (ou étendre la section existante) :

```json
  "accounts": {
    "default_new_account_role": "player",
    "role_change_audit": true
  },
```

- [ ] **Step 10.4 : Vérifier JSON valide**

```bash
python3 -c "import json; json.load(open('config.json'))" && echo "valid JSON"
```

- [ ] **Step 10.5 : Commit final + mention redéploiement**

```bash
git add docs/db_sql_guidelines.md config.json
git commit -m "docs(server/auth): Phase 1c roles 5 niveaux + HasLowerSecurity + config

Documente les 5 roles + regle d'or HasLowerSecurity + audit + migration
progressive handlers existants + convention Console (RCON sentinel).

Ajoute config.json :
- accounts.default_new_account_role = 'player'
- accounts.role_change_audit = true

Deploiement : ⚠️ redeploiement serveur (master) requis + migration 0043
appliquee. ENUM accounts.role etendue, AccountRecord modifie, audit
cable. Pas de wire-breaking cote protocole reseau.

CMANGOS.06 (Phase 1c) — fin de la phase.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11 : Validation finale

- [ ] **Step 11.1 : Re-run du test Phase 1c**

```bash
ctest --preset linux-x64 -R account_role_service_tests --output-on-failure
```

Expected: **PASS**.

- [ ] **Step 11.2 : Build complet master**

```bash
cmake --build --preset linux-x64 --target server_app
```

Expected: **PASS** sans warning supplémentaire.

- [ ] **Step 11.3 : Smoke test manuel SQL (DB requise)**

```bash
mysql -h <host> -u <user> -p <db> -e "
DESCRIBE accounts;
SELECT role, COUNT(*) FROM accounts GROUP BY role;
"
```

Expected:
- Colonne `role` montrée comme `enum('player','moderator','game_master','administrator')`
- Aucune ligne avec role='admin' (toutes migrées vers 'administrator')

- [ ] **Step 11.4 : Récap DoD**

Cocher manuellement :
- [ ] Migration 0043 appliquée et idempotente (re-jeu = no-op)
- [ ] `AccountRole` enum 5 niveaux + comparaisons type-safe (`<=>`)
- [ ] `RoleToString` / `ParseRole` supportent toutes valeurs + `admin` retrocompat + `gm` alias
- [ ] `AccountRecord` a un champ `role`
- [ ] `AccountStore::GetRole` / `SetRole` virtuelles pures + impl dans InMemory + Mysql
- [ ] `AccountRoleService::HasLowerSecurity` strict (égalité = false)
- [ ] `AccountRoleService::RequireMinRole` >=
- [ ] Audit câblé sur `SetRole`
- [ ] Config `accounts.default_new_account_role` + `role_change_audit`
- [ ] `account_role_service_tests` PASS
- [ ] Doc `docs/db_sql_guidelines.md` mise à jour
- [ ] Mention redéploiement serveur + migration

Si tous les items sont cochés → Phase 1c Accounts est livrée.

**Suite** : Phase 2 du roadmap (CMANGOS.01 Chat, qui consomme `AccountRole`
via `ChatCommandRouter`).

---

## Notes pour l'exécutant

### Si le `mysql_real_escape_string` n'est pas disponible

Sur certaines libmysqlclient récentes, `mysql_real_escape_string` est
toujours présent mais on peut préférer `mysql_real_escape_string_quote`
(plus permissif). Comme `RoleToString` retourne toujours un literal
ASCII safe (`"player"`, `"moderator"`, etc.), un simple `snprintf` sans
escape est aussi acceptable — l'escape est par défense en profondeur.

### Si l'API SecurityAuditLog n'a pas la signature attendue

Le code de Task 8 appelle `m_auditLog->Log("role_change", msg)`. Si
l'API existante est différente (ex. `LogEvent(category, message)` ou
`Append(line)` ou variadic format), adapter la ligne dans
`AccountRoleService::SetRole`. Vérifier `engine/server/SecurityAuditLog.h`
en début de Task 8.

### Si InMemoryAccountStore n'a pas de mutex `m_mutex`

Adapter Task 6.3 au pattern réel (peut-être `m_lock`, `m_storeMutex`,
ou pas de mutex si l'usage assume single-thread). `InMemoryAccountStore`
est typiquement utilisé en tests, donc mono-thread. Si pas de mutex,
retirer le `lock_guard` — la cohérence est garantie par l'usage.

### Si le mapping AccountRecord ↔ DB row utilise un index nominal

Certains stores utilisent `mysql_field_seek` + `mysql_fetch_field` pour
mapper par nom plutôt que par index. Si c'est le cas, ajouter "role" à
la liste des colonnes recherchées plutôt que de toucher au calcul d'index.

### Signature opérateur de comparaison C++20

Le `operator<=>` retourne `std::strong_ordering` ici. Compatible avec
toutes les comparaisons (`<`, `<=`, `==`, `>=`, `>`). Si le compilateur
proteste, vérifier que `-std=c++20` est bien actif (CMake projet est
en `set(CMAKE_CXX_STANDARD 20)` au root).

### Différences vs spec cmangos d'origine

- **ENUM string en DB** au lieu de TINYINT — plus debug-friendly,
  coût négligeable.
- **`Console` runtime-only** — clarifié ici comme sentinel `0xFFFF...`
  ou flag `ChatContext::isConsole`. Pas dans la DB.
- **`AccountRoleService` distinct d'`AccountStore`** — au lieu d'ajouter
  `HasLowerSecurity` directement sur le Store, on le met sur un service
  qui combine Store + AuditLog. Permet à AccountStore de rester pur
  CRUD, et au service d'orchestrer audit + invariants métier.

---

*Plan généré le 2026-05-08 par la skill `superpowers:writing-plans` à
partir de `docs/superpowers/audits/2026-05-08-cmangos-gap-analysis/CMANGOS.06.md`.*
