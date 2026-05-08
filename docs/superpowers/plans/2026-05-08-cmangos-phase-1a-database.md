# CMANGOS Phase 1a — Database (SQLStorage / SqlPreparedStatement / SqlDelayThread) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Étendre la couche d'accès DB du serveur LCDLLN (master + shard linux) avec 3 patterns cmangos déblocants amont : `SQLStorage<T>` (cache RAM typé chargé une fois au boot), `SqlPreparedStatement` (wrapper `MYSQL_STMT` avec binding type-safe), et `SqlDelayThread` (worker async avec queue + futures). Ces 3 utilitaires débloquent ~5 tickets P2 downstream qui consomment des tables statiques en lecture, des hot paths SQL, ou des opérations DB hors tick.

**Architecture:** On bâtit sur l'existant (`engine/server/db/ConnectionPool.h`, `DbHelpers.h`) en ajoutant 3 fichiers d'en-tête dans le même namespace `engine::server::db`. `SQLStorage` est header-only templated (instanciation dans le code consommateur). `SqlPreparedStatement` et `SqlDelayThread` sont en `.h`/`.cpp`. Trois exécutables de tests dédiés (custom `main()` retournant 0/1) sont ajoutés au pattern existant de `DbLayerTests`.

**Tech Stack:** C++20, MySQL C client (`<mysql.h>`), `std::thread`/`std::mutex`/`std::condition_variable`/`std::future`, `nlohmann/json` (déjà présent), CMake 3.20+, namespace `engine::server::db`. UNIX-only (la build Windows shard n'a pas MySQL). Tests : pattern `main()` standalone, skip si `db.host` non configuré, return 0/1, log via `LOG_INFO`/`LOG_ERROR`.

---

## Périmètre verrouillé

### Fichiers créés

- `engine/server/db/SQLStorage.h` — templated header-only
- `engine/server/db/SQLStorageTests.cpp` — exécutable test
- `engine/server/db/SqlPreparedStatement.h` — interface
- `engine/server/db/SqlPreparedStatement.cpp` — implémentation
- `engine/server/db/SqlPreparedStatementTests.cpp` — exécutable test
- `engine/server/db/SqlDelayThread.h` — interface
- `engine/server/db/SqlDelayThread.cpp` — implémentation
- `engine/server/db/SqlDelayThreadTests.cpp` — exécutable test
- `db/migrations/0041_phase_1a_test_storage.sql` — migration DB pour tester `SQLStorage` (table read-only minimale)

### Fichiers modifiés

- `engine/server/CMakeLists.txt` — ajouter 3 cibles de tests UNIX (`sql_storage_tests`, `sql_prepared_statement_tests`, `sql_delay_thread_tests`)

### Hors scope (à plaquer dans une autre PR)

- Migration de queries hot path existantes vers `SqlPreparedStatement` — sera fait au cas par cas dans les tickets downstream qui consomment `SqlPreparedStatement` (CMANGOS.07/.17/etc.)
- Migration de l'audit log vers `SqlDelayThread` — idem
- Hot-reload `.reload <storage_name>` GM command — dépend de CMANGOS.01 ChatCommandRouter, hors scope Phase 1a

### Convention de commits

- Un commit par task (pas de gros commit en fin)
- Format : `feat(server/db): <ce que ça fait>` ou `test(server/db): <ce que ça teste>` ou `build(server/db): <CMake>`
- Co-author Claude obligatoire
- Marquer `Déploiement : ⚠️ redéploiement serveur (master+shard) requis` à la fin du dernier commit (nouvelles capacités runtime)

---

## Décisions architecturales clés

### 1. SQLStorage thread-safety

- Le cache est **read-only après `Load()`**. Pas de lock pour les lookups.
- Le hot-reload (futur ticket) utilisera `std::shared_ptr<const StorageT>` swap atomique. Pour Phase 1a, **pas de hot-reload** : `Load()` peut être appelé une seule fois au boot, sinon assertion.

### 2. SqlPreparedStatement cache

- Cache LRU par connexion, taille bornée (config `db.prepared_statement_cache_size_per_conn`, défaut 64).
- Clé du cache = SQL string (hash). Si un statement est éjecté, sa fin de vie (`mysql_stmt_close`) est immédiate.

### 3. SqlDelayThread queue overflow policy

- Queue bornée (config `db.delay_thread_queue_size`, défaut 1024).
- Politique en cas de queue pleine : **reject** (la fonction `EnqueueExecute` retourne `false`, le caller doit gérer). Pas de blocking ni de drop silencieux.
- À la `Stop()`, drain la queue restante avant join du thread (safe shutdown).

### 4. Pas de migration DB destructive

- La migration `0041_phase_1a_test_storage.sql` crée **uniquement** une table de test minimaliste pour exercer `SQLStorage`. Pas de modification de tables existantes.
- Idempotente (`CREATE TABLE IF NOT EXISTS`), seedée avec 3 lignes pour les tests.

---

## Task 1 : Créer la migration de test

**Files:**
- Create: `db/migrations/0041_phase_1a_test_storage.sql`

- [ ] **Step 1.1 : Créer le fichier de migration**

```sql
-- 0041 — Phase 1a CMANGOS Database : table de test minimaliste pour SQLStorage<T>.
-- Read-only après seed initial. Utilisée par engine/server/db/SQLStorageTests.cpp.

CREATE TABLE IF NOT EXISTS phase_1a_test_storage (
  entry        INT UNSIGNED NOT NULL,
  name         VARCHAR(64) NOT NULL,
  value        INT NOT NULL,
  PRIMARY KEY (entry)
);

-- Seed 3 lignes idempotentes (UPSERT pattern via INSERT IGNORE).
INSERT IGNORE INTO phase_1a_test_storage (entry, name, value) VALUES
  (1, 'alpha', 100),
  (2, 'beta', 200),
  (3, 'gamma', 300);
```

- [ ] **Step 1.2 : Commit**

```bash
git add db/migrations/0041_phase_1a_test_storage.sql
git commit -m "feat(db): migration 0041 phase_1a_test_storage table

Table de test idempotente pour exercer SQLStorage<T> dans les tests du
ticket CMANGOS.13 (Phase 1a Database).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2 : SQLStorage<T> — squelette + signature compile

**Files:**
- Create: `engine/server/db/SQLStorage.h`

- [ ] **Step 2.1 : Créer le header avec interface complète mais Load() non implémentée**

```cpp
#pragma once
// CMANGOS.13 (Phase 1a) — SQLStorage<T> : cache RAM typé read-only chargé une
// fois au boot depuis une table SQL. Lookup O(1), pas de lock post-load.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

struct MYSQL;

namespace engine::server::db
{
	class ConnectionPool;

	/// Cache RAM typé d'une table SQL read-only.
	///
	/// Usage type :
	/// ```cpp
	/// struct CreatureTemplate {
	///     uint32_t entry;
	///     std::string name;
	///     int32_t level;
	/// };
	///
	/// SQLStorage<CreatureTemplate> g_creatureTemplates;
	/// g_creatureTemplates.Load(pool, "creature_template", "entry",
	///     [](MYSQL_ROW row) -> CreatureTemplate {
	///         CreatureTemplate t{};
	///         t.entry = std::strtoul(row[0], nullptr, 10);
	///         t.name  = row[1] ? row[1] : "";
	///         t.level = std::atoi(row[2]);
	///         return t;
	///     });
	/// const CreatureTemplate* tmpl = g_creatureTemplates.Find(42);
	/// ```
	///
	/// Thread-safety : Load() doit être appelé une seule fois (assertion sinon).
	/// Après Load(), Find() et l'itération sont thread-safe lecture concurrente
	/// (pas de mutation post-load).
	template <typename T>
	class SQLStorage
	{
	public:
		using RowMapper = std::function<T(char** row)>;

		SQLStorage() = default;
		~SQLStorage() = default;

		SQLStorage(const SQLStorage&) = delete;
		SQLStorage& operator=(const SQLStorage&) = delete;

		/// Charge toutes les lignes de \p tableName en RAM.
		/// \p pkColumn est utilisé pour générer la requête `SELECT * ORDER BY <pk>`.
		/// \p mapper convertit chaque MYSQL_ROW en T (les colonnes brutes sont des
		/// `char*` pouvant être null).
		/// Retourne true si OK, false si requête échoue (et le storage reste vide).
		/// \pre Load() ne doit être appelée qu'une fois (assertion sinon).
		bool Load(ConnectionPool& pool, std::string_view tableName,
			std::string_view pkColumn, RowMapper mapper);

		/// Retourne un pointeur vers l'entrée si trouvée, nullptr sinon.
		/// Le pointeur reste valide tant que SQLStorage existe (pas de mutation
		/// post-load).
		const T* Find(uint32_t pk) const;

		/// Nombre d'entrées chargées.
		size_t Size() const { return m_entries.size(); }

		/// Itération (par ex. pour valider au boot).
		auto begin() const { return m_entries.cbegin(); }
		auto end()   const { return m_entries.cend(); }

	private:
		std::unordered_map<uint32_t, T> m_entries;
		bool m_loaded = false;
	};
}
```

- [ ] **Step 2.2 : Vérifier que ça compile sans erreur**

Le header est templaté ; comme aucun consommateur ne l'inclut encore, la compilation est triviale (pas de symbole à résoudre). On vérifiera plus tard avec le test.

```bash
# Pas de commande à exécuter à cette étape — compilation différée à Task 5.
```

- [ ] **Step 2.3 : Commit**

```bash
git add engine/server/db/SQLStorage.h
git commit -m "feat(server/db): SQLStorage<T> header (signature only)

Squelette templated du cache RAM read-only. Implémentation Load() suit
dans la prochaine tâche (TDD).

CMANGOS.13 (Phase 1a) — pré-requis amont pour ~5 P2 downstream.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3 : SQLStorage<T> — test failing pour Load()

**Files:**
- Create: `engine/server/db/SQLStorageTests.cpp`
- Modify: `engine/server/CMakeLists.txt` (ajout cible test)

- [ ] **Step 3.1 : Créer le test exécutable (suit le pattern DbLayerTests)**

```cpp
// CMANGOS.13 (Phase 1a) — Tests SQLStorage<T> : Load + Find + Size + iteration.
// Pattern DbLayerTests : skip silencieux si db.host non configuré.

#include "engine/server/db/SQLStorage.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <mysql.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	/// Struct de test mappant `phase_1a_test_storage`.
	struct TestEntry
	{
		uint32_t entry = 0;
		std::string name;
		int32_t value = 0;
	};

	/// Mapper MYSQL_ROW → TestEntry. row[0]=entry, row[1]=name, row[2]=value.
	TestEntry MapRow(char** row)
	{
		TestEntry e{};
		e.entry = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
		e.name  = row[1] ? row[1] : "";
		e.value = std::atoi(row[2]);
		return e;
	}

	bool CheckLoadFindIterate(engine::server::db::ConnectionPool& pool)
	{
		engine::server::db::SQLStorage<TestEntry> storage;
		const bool ok = storage.Load(pool, "phase_1a_test_storage", "entry", MapRow);
		if (!ok)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Load() returned false");
			return false;
		}
		if (storage.Size() != 3u)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Size expected 3, got {}", storage.Size());
			return false;
		}
		const TestEntry* alpha = storage.Find(1);
		if (!alpha || alpha->name != "alpha" || alpha->value != 100)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Find(1) failed");
			return false;
		}
		const TestEntry* gamma = storage.Find(3);
		if (!gamma || gamma->name != "gamma" || gamma->value != 300)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Find(3) failed");
			return false;
		}
		if (storage.Find(999) != nullptr)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Find(999) should return nullptr");
			return false;
		}
		// Iteration : les 3 entrées doivent toutes être visitées.
		size_t count = 0;
		for (const auto& [pk, entry] : storage)
		{
			(void)pk; (void)entry;
			++count;
		}
		if (count != 3u)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Iteration count expected 3, got {}", count);
			return false;
		}
		LOG_INFO(Core, "[SQLStorageTests] Load+Find+Size+Iterate OK");
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
		LOG_INFO(Core, "[SQLStorageTests] db.host not set, skipping (smoke test optional without DB)");
		engine::core::Log::Shutdown();
		return 0;
	}

	engine::server::db::ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[SQLStorageTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	const bool ok = CheckLoadFindIterate(pool);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 3.2 : Ajouter la cible CMake (UNIX uniquement, dans la section MySQL)**

Localiser dans `engine/server/CMakeLists.txt` la ligne `add_test(NAME db_layer_tests COMMAND db_layer_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})` (autour de la ligne 154). **Ajouter juste après** :

```cmake
  # CMANGOS.13 (Phase 1a) : SQLStorage<T> tests
  add_executable(sql_storage_tests db/SQLStorageTests.cpp db/ConnectionPool.cpp db/DbHelpers.cpp)
  target_include_directories(sql_storage_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_storage_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_storage_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_storage_tests COMMAND sql_storage_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 3.3 : Build et vérifier que le test échoue à la compilation (Load() pas implémenté)**

```bash
cmake --preset linux-x64
cmake --build --preset linux-x64 --target sql_storage_tests
```

Expected: **FAIL** with linker error like `undefined reference to engine::server::db::SQLStorage<TestEntry>::Load(...)`.

C'est attendu : on a écrit la signature mais pas l'implémentation. Le TDD continue à Task 4.

- [ ] **Step 3.4 : Commit (test failing)**

```bash
git add engine/server/db/SQLStorageTests.cpp engine/server/CMakeLists.txt
git commit -m "test(server/db): SQLStorageTests squelette (failing — Load not implemented)

Test exécutable suivant le pattern DbLayerTests : skip si db.host vide,
sinon Init pool puis exerce Load+Find+Size+Iterate sur la table
phase_1a_test_storage.

L'étape de build attend une erreur de link sur Load() qui sera résolue
dans la tâche suivante (TDD red).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4 : SQLStorage<T> — implémentation Load() / Find() / iteration

**Files:**
- Modify: `engine/server/db/SQLStorage.h` (ajout définitions inline templated)

- [ ] **Step 4.1 : Ajouter les implémentations inline en fin du header**

Localiser la fin du fichier `engine/server/db/SQLStorage.h` (juste avant la fermeture `}` du namespace). **Ajouter avant cette accolade fermante** :

```cpp
	// ─────────────── Implémentation inline (templated, doit rester en header) ───────────────

	template <typename T>
	bool SQLStorage<T>::Load(ConnectionPool& pool, std::string_view tableName,
		std::string_view pkColumn, RowMapper mapper)
	{
		if (m_loaded)
		{
			// Spec : Load() doit être appelé une seule fois. Sinon, le caller
			// a probablement un bug de séquençage. On échoue plutôt que d'écraser
			// silencieusement le cache.
			return false;
		}

		auto guard = pool.Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql)
			return false;

		// Construit "SELECT * FROM <table> ORDER BY <pk>" — quoting backtick pour
		// supporter les noms réservés.
		std::string sql;
		sql.reserve(64 + tableName.size() + pkColumn.size());
		sql.append("SELECT * FROM `");
		sql.append(tableName);
		sql.append("` ORDER BY `");
		sql.append(pkColumn);
		sql.append("`");

		if (mysql_query(mysql, sql.c_str()) != 0)
			return false;

		MYSQL_RES* res = mysql_store_result(mysql);
		if (!res)
			return false;

		MYSQL_ROW row;
		while ((row = mysql_fetch_row(res)) != nullptr)
		{
			if (!row[0])
				continue;  // PK NULL : ligne corrompue, on saute.
			T entry = mapper(row);
			const uint32_t pk = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
			m_entries.emplace(pk, std::move(entry));
		}
		mysql_free_result(res);

		m_loaded = true;
		return true;
	}

	template <typename T>
	const T* SQLStorage<T>::Find(uint32_t pk) const
	{
		auto it = m_entries.find(pk);
		if (it == m_entries.end())
			return nullptr;
		return &it->second;
	}
```

- [ ] **Step 4.2 : Build le test**

```bash
cmake --build --preset linux-x64 --target sql_storage_tests
```

Expected: **PASS** compilation. Si erreur, vérifier que `<mysql.h>` est inclus (à ajouter dans le header ?).

**Si le build échoue à cause de `mysql_query`/`mysql_store_result`/`mysql_fetch_row` non déclarés** : il faut inclure `<mysql.h>` dans `SQLStorage.h`. Localiser la ligne `struct MYSQL;` et la **remplacer par** :

```cpp
#include <mysql.h>
```

Puis rebuild.

- [ ] **Step 4.3 : Lancer le test (DB requise)**

D'abord, appliquer la migration 0041 sur la DB de test (dépend du process MigrationRunner du projet — typiquement automatique au boot du serveur). Vérifier manuellement :

```bash
mysql -h <host> -u <user> -p <db> -e "SELECT * FROM phase_1a_test_storage;"
```

Expected output : 3 lignes (alpha/beta/gamma).

Puis lancer le test :

```bash
ctest --preset linux-x64 -R sql_storage_tests --output-on-failure
```

Expected: **PASS** avec `[SQLStorageTests] Load+Find+Size+Iterate OK` dans le log.

- [ ] **Step 4.4 : Commit**

```bash
git add engine/server/db/SQLStorage.h
git commit -m "feat(server/db): SQLStorage<T> Load+Find+iteration (TDD green)

Implémentation header-only de la lecture de table SQL en cache RAM typé.
Test sql_storage_tests passe contre phase_1a_test_storage (3 entrées).

CMANGOS.13 (Phase 1a) — déblocant amont pour SQLStorage utilisé par les
tickets P2 downstream.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5 : SQLStorage<T> — test du "Load deux fois → false"

**Files:**
- Modify: `engine/server/db/SQLStorageTests.cpp`

- [ ] **Step 5.1 : Ajouter une fonction de test pour le double-load**

Localiser dans `engine/server/db/SQLStorageTests.cpp` la fonction `CheckLoadFindIterate`. **Ajouter juste après** sa définition (avant `int main`) :

```cpp
	bool CheckDoubleLoadRejected(engine::server::db::ConnectionPool& pool)
	{
		engine::server::db::SQLStorage<TestEntry> storage;
		const bool ok1 = storage.Load(pool, "phase_1a_test_storage", "entry", MapRow);
		if (!ok1)
		{
			LOG_ERROR(Core, "[SQLStorageTests] First Load failed");
			return false;
		}
		const bool ok2 = storage.Load(pool, "phase_1a_test_storage", "entry", MapRow);
		if (ok2)
		{
			LOG_ERROR(Core, "[SQLStorageTests] Second Load should have returned false");
			return false;
		}
		LOG_INFO(Core, "[SQLStorageTests] Double-load rejected OK");
		return true;
	}
```

- [ ] **Step 5.2 : Câbler le test dans `main()`**

Localiser dans `int main` la ligne `const bool ok = CheckLoadFindIterate(pool);`. **Remplacer** par :

```cpp
	const bool ok = CheckLoadFindIterate(pool) && CheckDoubleLoadRejected(pool);
```

- [ ] **Step 5.3 : Build + run**

```bash
cmake --build --preset linux-x64 --target sql_storage_tests
ctest --preset linux-x64 -R sql_storage_tests --output-on-failure
```

Expected: **PASS** avec les deux logs `Load+Find+Size+Iterate OK` puis `Double-load rejected OK`.

- [ ] **Step 5.4 : Commit**

```bash
git add engine/server/db/SQLStorageTests.cpp
git commit -m "test(server/db): SQLStorage rejects second Load() call

Couvre le contrat \"Load() une seule fois\" — un second appel doit retourner
false sans écraser le cache. Évite des bugs subtils de séquençage au boot.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6 : SqlPreparedStatement — interface .h

**Files:**
- Create: `engine/server/db/SqlPreparedStatement.h`

- [ ] **Step 6.1 : Écrire l'en-tête complet (interface)**

```cpp
#pragma once
// CMANGOS.13 (Phase 1a) — SqlPreparedStatement : wrapper C++20 autour de
// MYSQL_STMT avec binding type-safe et cache LRU par connexion.

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct MYSQL;
struct MYSQL_STMT;

namespace engine::server::db
{
	/// Wrapper RAII autour de `MYSQL_STMT`. Ne pas instancier directement —
	/// utiliser `SqlPreparedStatementCache::Acquire(mysql, sql)`.
	class SqlPreparedStatement
	{
	public:
		~SqlPreparedStatement();
		SqlPreparedStatement(const SqlPreparedStatement&) = delete;
		SqlPreparedStatement& operator=(const SqlPreparedStatement&) = delete;

		/// Bind un argument à la position \p pos (0-indexé). Surcharges pour les
		/// types courants. Doit être appelé avant Execute().
		bool Bind(size_t pos, int32_t value);
		bool Bind(size_t pos, int64_t value);
		bool Bind(size_t pos, uint32_t value);
		bool Bind(size_t pos, uint64_t value);
		bool Bind(size_t pos, double value);
		bool Bind(size_t pos, std::string_view value);
		/// Bind blob brut (la mémoire pointée doit rester valide jusqu'à Execute()).
		bool BindBlob(size_t pos, const void* data, size_t size);

		/// Exécute le statement. Retourne true si OK. Pour SELECT, voir FetchRow.
		bool Execute();

		/// Pour SELECT : récupère la prochaine ligne. Retourne false si plus de lignes
		/// (ou erreur). Les valeurs sont accessibles via GetInt/GetString après chaque
		/// FetchRow réussi.
		bool FetchRow();

		/// Lecteurs de colonne après FetchRow (0-indexés sur les colonnes du SELECT).
		int32_t  GetInt32(size_t col, int32_t fallback = 0) const;
		uint64_t GetUInt64(size_t col, uint64_t fallback = 0) const;
		std::string GetString(size_t col) const;

		/// Réinitialise les bindings et l'état pour une nouvelle exécution.
		/// Le statement reste valide, on peut Bind+Execute à nouveau.
		bool Reset();

		/// Accès au handle brut MYSQL_STMT (pour cas très spécifiques uniquement).
		MYSQL_STMT* Handle() const { return m_stmt; }

	private:
		// Construction via SqlPreparedStatementCache uniquement.
		friend class SqlPreparedStatementCache;
		SqlPreparedStatement(MYSQL_STMT* stmt, size_t paramCount, size_t resultColumnCount);

		MYSQL_STMT* m_stmt = nullptr;
		size_t m_paramCount = 0;
		size_t m_resultColumnCount = 0;
		// Buffers pour les bindings d'entrée (longueurs, types, données).
		// Stockés ici pour rester en vie jusqu'à Execute().
		std::vector<std::vector<uint8_t>> m_paramBuffers;
		std::vector<unsigned long> m_paramLengths;
		std::vector<char> m_paramIsNull;  // char car MYSQL_BIND::is_null veut un my_bool*
		// Type MYSQL pour chaque param (MYSQL_TYPE_LONG/LONGLONG/DOUBLE/STRING/BLOB).
		// Stocké comme int pour éviter d'inclure <mysql.h> dans le header public.
		std::vector<int> m_paramTypes;
		// Drapeau "non signé" pour les types entiers (MYSQL_BIND::is_unsigned).
		std::vector<char> m_paramIsUnsigned;
		// Buffers pour les colonnes de résultat (alloués au premier Execute).
		std::vector<std::vector<uint8_t>> m_resultBuffers;
		std::vector<unsigned long> m_resultLengths;
		std::vector<char> m_resultIsNull;
	};

	/// Cache LRU de SqlPreparedStatement par connexion MYSQL*.
	/// Thread-safety : un cache appartient à un seul thread (typiquement,
	/// un cache par worker DB). Pas de mutex interne.
	class SqlPreparedStatementCache
	{
	public:
		explicit SqlPreparedStatementCache(size_t maxEntries);
		~SqlPreparedStatementCache();
		SqlPreparedStatementCache(const SqlPreparedStatementCache&) = delete;
		SqlPreparedStatementCache& operator=(const SqlPreparedStatementCache&) = delete;

		/// Acquiert (ou crée) un statement pour \p sql sur \p mysql. Retourne
		/// nullptr si la préparation échoue. Le pointeur reste valide jusqu'à
		/// l'éviction LRU ou la destruction du cache.
		SqlPreparedStatement* Acquire(MYSQL* mysql, std::string_view sql);

		/// Nombre d'entrées actuellement dans le cache.
		size_t Size() const { return m_lru.size(); }

	private:
		struct Entry
		{
			std::string sql;
			std::unique_ptr<SqlPreparedStatement> stmt;
		};

		size_t m_maxEntries;
		std::list<Entry> m_lru;  // front = most recent, back = least recent
		std::unordered_map<std::string, std::list<Entry>::iterator> m_index;
	};
}
```

- [ ] **Step 6.2 : Commit**

```bash
git add engine/server/db/SqlPreparedStatement.h
git commit -m "feat(server/db): SqlPreparedStatement + Cache header (signature only)

Interface complète : RAII, binding type-safe (int32/64, uint32/64, double,
string, blob), execute, fetch row, reset, et un cache LRU par connexion.

CMANGOS.13 (Phase 1a) — implémentation suit dans la prochaine tâche.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7 : SqlPreparedStatement — implémentation .cpp

**Files:**
- Create: `engine/server/db/SqlPreparedStatement.cpp`

- [ ] **Step 7.1 : Implémentation complète**

```cpp
#include "engine/server/db/SqlPreparedStatement.h"

#include <mysql.h>

#include <cstring>

namespace engine::server::db
{
	namespace
	{
		MYSQL_STMT* PrepareStatement(MYSQL* mysql, std::string_view sql)
		{
			MYSQL_STMT* stmt = mysql_stmt_init(mysql);
			if (!stmt)
				return nullptr;
			if (mysql_stmt_prepare(stmt, sql.data(), static_cast<unsigned long>(sql.size())) != 0)
			{
				mysql_stmt_close(stmt);
				return nullptr;
			}
			return stmt;
		}
	}

	SqlPreparedStatement::SqlPreparedStatement(MYSQL_STMT* stmt, size_t paramCount, size_t resultColumnCount)
		: m_stmt(stmt)
		, m_paramCount(paramCount)
		, m_resultColumnCount(resultColumnCount)
	{
		if (paramCount > 0)
		{
			m_paramBuffers.resize(paramCount);
			m_paramLengths.assign(paramCount, 0);
			m_paramIsNull.assign(paramCount, 0);
			m_paramTypes.assign(paramCount, MYSQL_TYPE_NULL);
			m_paramIsUnsigned.assign(paramCount, 0);
		}
		if (resultColumnCount > 0)
		{
			m_resultBuffers.assign(resultColumnCount, std::vector<uint8_t>(256));
			m_resultLengths.assign(resultColumnCount, 0);
			m_resultIsNull.assign(resultColumnCount, 0);
		}
	}

	SqlPreparedStatement::~SqlPreparedStatement()
	{
		if (m_stmt)
		{
			mysql_stmt_close(m_stmt);
			m_stmt = nullptr;
		}
	}

	bool SqlPreparedStatement::Bind(size_t pos, int32_t value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(int32_t));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(int32_t));
		m_paramLengths[pos] = sizeof(int32_t);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_LONG;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, int64_t value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(int64_t));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(int64_t));
		m_paramLengths[pos] = sizeof(int64_t);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_LONGLONG;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, uint32_t value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(uint32_t));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(uint32_t));
		m_paramLengths[pos] = sizeof(uint32_t);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_LONG;
		m_paramIsUnsigned[pos] = 1;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, uint64_t value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(uint64_t));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(uint64_t));
		m_paramLengths[pos] = sizeof(uint64_t);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_LONGLONG;
		m_paramIsUnsigned[pos] = 1;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, double value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].resize(sizeof(double));
		std::memcpy(m_paramBuffers[pos].data(), &value, sizeof(double));
		m_paramLengths[pos] = sizeof(double);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_DOUBLE;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::Bind(size_t pos, std::string_view value)
	{
		if (pos >= m_paramCount)
			return false;
		m_paramBuffers[pos].assign(value.begin(), value.end());
		m_paramLengths[pos] = static_cast<unsigned long>(value.size());
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_STRING;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::BindBlob(size_t pos, const void* data, size_t size)
	{
		if (pos >= m_paramCount)
			return false;
		const auto* p = static_cast<const uint8_t*>(data);
		m_paramBuffers[pos].assign(p, p + size);
		m_paramLengths[pos] = static_cast<unsigned long>(size);
		m_paramIsNull[pos] = 0;
		m_paramTypes[pos] = MYSQL_TYPE_BLOB;
		m_paramIsUnsigned[pos] = 0;
		return true;
	}

	bool SqlPreparedStatement::Execute()
	{
		if (!m_stmt)
			return false;

		// Bind input parameters — utilise m_paramTypes (set par chaque Bind*) pour
		// éviter l'heuristique sur la taille (qui confondait double/int64 sur 8 bytes).
		if (m_paramCount > 0)
		{
			std::vector<MYSQL_BIND> binds(m_paramCount);
			std::memset(binds.data(), 0, sizeof(MYSQL_BIND) * m_paramCount);
			for (size_t i = 0; i < m_paramCount; ++i)
			{
				binds[i].buffer = m_paramBuffers[i].data();
				binds[i].buffer_length = static_cast<unsigned long>(m_paramBuffers[i].size());
				binds[i].length = &m_paramLengths[i];
				binds[i].buffer_type = static_cast<enum_field_types>(m_paramTypes[i]);
				binds[i].is_unsigned = m_paramIsUnsigned[i] != 0;
				binds[i].is_null = reinterpret_cast<bool*>(&m_paramIsNull[i]);
			}
			if (mysql_stmt_bind_param(m_stmt, binds.data()) != 0)
				return false;
		}

		// Bind output columns (pour SELECT).
		if (m_resultColumnCount > 0)
		{
			std::vector<MYSQL_BIND> rbinds(m_resultColumnCount);
			std::memset(rbinds.data(), 0, sizeof(MYSQL_BIND) * m_resultColumnCount);
			for (size_t i = 0; i < m_resultColumnCount; ++i)
			{
				rbinds[i].buffer = m_resultBuffers[i].data();
				rbinds[i].buffer_length = static_cast<unsigned long>(m_resultBuffers[i].size());
				rbinds[i].length = &m_resultLengths[i];
				rbinds[i].is_null = reinterpret_cast<bool*>(&m_resultIsNull[i]);
				rbinds[i].buffer_type = MYSQL_TYPE_STRING;  // simpliste, on lit en string
			}
			if (mysql_stmt_bind_result(m_stmt, rbinds.data()) != 0)
				return false;
		}

		return mysql_stmt_execute(m_stmt) == 0;
	}

	bool SqlPreparedStatement::FetchRow()
	{
		if (!m_stmt)
			return false;
		const int rc = mysql_stmt_fetch(m_stmt);
		return rc == 0;  // 0 = OK, MYSQL_NO_DATA = fin, autres = erreur
	}

	int32_t SqlPreparedStatement::GetInt32(size_t col, int32_t fallback) const
	{
		if (col >= m_resultColumnCount || m_resultIsNull[col])
			return fallback;
		// Le buffer contient une string ASCII de l'entier (MYSQL_TYPE_STRING).
		const auto& buf = m_resultBuffers[col];
		const std::string s(reinterpret_cast<const char*>(buf.data()), m_resultLengths[col]);
		return std::atoi(s.c_str());
	}

	uint64_t SqlPreparedStatement::GetUInt64(size_t col, uint64_t fallback) const
	{
		if (col >= m_resultColumnCount || m_resultIsNull[col])
			return fallback;
		const auto& buf = m_resultBuffers[col];
		const std::string s(reinterpret_cast<const char*>(buf.data()), m_resultLengths[col]);
		return std::strtoull(s.c_str(), nullptr, 10);
	}

	std::string SqlPreparedStatement::GetString(size_t col) const
	{
		if (col >= m_resultColumnCount || m_resultIsNull[col])
			return {};
		const auto& buf = m_resultBuffers[col];
		return std::string(reinterpret_cast<const char*>(buf.data()), m_resultLengths[col]);
	}

	bool SqlPreparedStatement::Reset()
	{
		if (!m_stmt)
			return false;
		// Vide le résultat en cours (si SELECT en boucle).
		mysql_stmt_free_result(m_stmt);
		return mysql_stmt_reset(m_stmt) == 0;
	}

	// ─── Cache ───

	SqlPreparedStatementCache::SqlPreparedStatementCache(size_t maxEntries)
		: m_maxEntries(maxEntries)
	{
	}

	SqlPreparedStatementCache::~SqlPreparedStatementCache() = default;

	SqlPreparedStatement* SqlPreparedStatementCache::Acquire(MYSQL* mysql, std::string_view sql)
	{
		const std::string key(sql);

		// Hit : déplacer en tête (most recent) et retourner.
		auto it = m_index.find(key);
		if (it != m_index.end())
		{
			m_lru.splice(m_lru.begin(), m_lru, it->second);
			it->second = m_lru.begin();
			return it->second->stmt.get();
		}

		// Miss : préparer un nouveau statement.
		MYSQL_STMT* stmt = PrepareStatement(mysql, sql);
		if (!stmt)
			return nullptr;
		const size_t paramCount = mysql_stmt_param_count(stmt);
		MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
		const size_t resultColumnCount = meta ? mysql_num_fields(meta) : 0;
		if (meta)
			mysql_free_result(meta);

		// Eviction LRU si plein.
		if (m_lru.size() >= m_maxEntries && !m_lru.empty())
		{
			m_index.erase(m_lru.back().sql);
			m_lru.pop_back();
		}

		Entry entry{key, std::unique_ptr<SqlPreparedStatement>(
			new SqlPreparedStatement(stmt, paramCount, resultColumnCount))};
		m_lru.push_front(std::move(entry));
		m_index[key] = m_lru.begin();
		return m_lru.begin()->stmt.get();
	}
}
```

- [ ] **Step 7.2 : Build (sans test pour l'instant)**

```bash
cmake --build --preset linux-x64 --target server_app
```

Expected: **PASS** (le `server_app` ne référence pas encore SqlPreparedStatement, donc aucun changement attendu — on vérifie juste qu'on n'a pas cassé la compilation existante).

Si erreurs (ex. méthode manquante MYSQL\_BIND fields), vérifier la version de libmysqlclient (sur Ubuntu 22+ certains champs ont des aliases).

- [ ] **Step 7.3 : Commit**

```bash
git add engine/server/db/SqlPreparedStatement.cpp
git commit -m "feat(server/db): SqlPreparedStatement + Cache implementation

Bindings input via MYSQL_BIND avec type heuristique sur la taille du buffer
(4=LONG, 8=LONGLONG, autre=STRING). Output bindings en STRING simpliste
(GetInt32/GetUInt64/GetString reparsent). Cache LRU par list+unordered_map,
éviction à maxEntries dépassée.

CMANGOS.13 (Phase 1a) — tests dans la tâche suivante (TDD).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 8 : SqlPreparedStatement — tests cache hit/miss + bind/execute/fetch

**Files:**
- Create: `engine/server/db/SqlPreparedStatementTests.cpp`
- Modify: `engine/server/CMakeLists.txt`

- [ ] **Step 8.1 : Écrire le test exécutable**

```cpp
// CMANGOS.13 (Phase 1a) — Tests SqlPreparedStatement + Cache.

#include "engine/server/db/SqlPreparedStatement.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <mysql.h>

namespace
{
	using engine::server::db::SqlPreparedStatementCache;
	using engine::server::db::SqlPreparedStatement;
	using engine::server::db::ConnectionPool;

	bool TestBindExecuteFetch(MYSQL* mysql)
	{
		SqlPreparedStatementCache cache(8);
		SqlPreparedStatement* stmt = cache.Acquire(mysql,
			"SELECT name, value FROM phase_1a_test_storage WHERE entry = ?");
		if (!stmt)
		{
			LOG_ERROR(Core, "[SqlPSTests] Acquire failed (table 0041 missing?)");
			return false;
		}
		if (!stmt->Bind(0, static_cast<int32_t>(2)))
		{
			LOG_ERROR(Core, "[SqlPSTests] Bind(0, 2) failed");
			return false;
		}
		if (!stmt->Execute())
		{
			LOG_ERROR(Core, "[SqlPSTests] Execute failed");
			return false;
		}
		if (!stmt->FetchRow())
		{
			LOG_ERROR(Core, "[SqlPSTests] FetchRow failed (no data?)");
			return false;
		}
		const std::string name = stmt->GetString(0);
		const int32_t value = stmt->GetInt32(1);
		if (name != "beta" || value != 200)
		{
			LOG_ERROR(Core, "[SqlPSTests] Expected beta/200, got {}/{}", name, value);
			return false;
		}
		LOG_INFO(Core, "[SqlPSTests] BindExecuteFetch beta/200 OK");
		return true;
	}

	bool TestCacheHitMiss(MYSQL* mysql)
	{
		SqlPreparedStatementCache cache(2);  // capacité 2 pour forcer éviction
		const char* sql1 = "SELECT name FROM phase_1a_test_storage WHERE entry = ?";
		const char* sql2 = "SELECT value FROM phase_1a_test_storage WHERE entry = ?";
		const char* sql3 = "SELECT entry FROM phase_1a_test_storage WHERE entry = ?";

		SqlPreparedStatement* s1 = cache.Acquire(mysql, sql1);
		SqlPreparedStatement* s2 = cache.Acquire(mysql, sql2);
		if (!s1 || !s2 || cache.Size() != 2u)
		{
			LOG_ERROR(Core, "[SqlPSTests] Cache size after 2 misses != 2");
			return false;
		}
		// Hit sur s1 : devrait retourner le même pointeur.
		SqlPreparedStatement* s1bis = cache.Acquire(mysql, sql1);
		if (s1bis != s1)
		{
			LOG_ERROR(Core, "[SqlPSTests] Cache hit on sql1 returned different pointer");
			return false;
		}
		// Miss sur s3 : éviction LRU (s2 doit sortir, pas s1 qui vient d'être acquis).
		SqlPreparedStatement* s3 = cache.Acquire(mysql, sql3);
		if (!s3 || cache.Size() != 2u)
		{
			LOG_ERROR(Core, "[SqlPSTests] Cache size after eviction != 2");
			return false;
		}
		// Re-acquérir s2 doit donner un nouveau pointeur (il a été évincé).
		SqlPreparedStatement* s2bis = cache.Acquire(mysql, sql2);
		if (s2bis == s2)
		{
			LOG_ERROR(Core, "[SqlPSTests] sql2 should have been evicted, got same pointer");
			return false;
		}
		LOG_INFO(Core, "[SqlPSTests] Cache hit/miss/LRU eviction OK");
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
		LOG_INFO(Core, "[SqlPSTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[SqlPSTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	auto guard = pool.Acquire();
	MYSQL* mysql = guard.get();
	bool ok = mysql && TestBindExecuteFetch(mysql) && TestCacheHitMiss(mysql);

	guard = ConnectionPool::Guard();
	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 8.2 : Ajouter la cible CMake**

Localiser dans `engine/server/CMakeLists.txt` la cible `sql_storage_tests` (ajoutée à Task 3.2). **Ajouter juste après** :

```cmake
  # CMANGOS.13 (Phase 1a) : SqlPreparedStatement + cache tests
  add_executable(sql_prepared_statement_tests
    db/SqlPreparedStatementTests.cpp
    db/SqlPreparedStatement.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(sql_prepared_statement_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_prepared_statement_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_prepared_statement_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_prepared_statement_tests COMMAND sql_prepared_statement_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 8.3 : Build + run**

```bash
cmake --build --preset linux-x64 --target sql_prepared_statement_tests
ctest --preset linux-x64 -R sql_prepared_statement_tests --output-on-failure
```

Expected: **PASS** — logs `BindExecuteFetch beta/200 OK` puis `Cache hit/miss/LRU eviction OK`.

- [ ] **Step 8.4 : Commit**

```bash
git add engine/server/db/SqlPreparedStatementTests.cpp engine/server/CMakeLists.txt
git commit -m "test(server/db): SqlPreparedStatement bind/execute/fetch + cache LRU

Tests :
- Bind int32 + Execute + FetchRow + GetString/GetInt32 sur entry=2
  (\"beta\", 200).
- Cache hit/miss : 2 misses, 1 hit (même pointeur), 1 miss force éviction
  LRU, le statement éviccé revient avec un nouveau pointeur.

CMANGOS.13 (Phase 1a) — TDD green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9 : SqlDelayThread — interface .h

**Files:**
- Create: `engine/server/db/SqlDelayThread.h`

- [ ] **Step 9.1 : Écrire l'en-tête complet**

```cpp
#pragma once
// CMANGOS.13 (Phase 1a) — SqlDelayThread : worker thread async qui consomme
// une queue d'opérations DB avec callbacks. Permet aux handlers de "fire and
// forget" un INSERT/UPDATE sans bloquer le tick.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

struct MYSQL;

namespace engine::server::db
{
	class ConnectionPool;

	/// Worker thread async pour exécution DB hors tick.
	///
	/// Usage type :
	/// ```cpp
	/// SqlDelayThread worker(pool, 1024);
	/// worker.Start();
	/// // ... plus tard, dans un handler chaud :
	/// worker.EnqueueExecute("INSERT INTO audit (...) VALUES (...)",
	///     [](bool ok) { if (!ok) LOG_WARN(Core, "audit failed"); });
	/// // ... à la fin du process :
	/// worker.Stop();
	/// ```
	///
	/// Thread-safety : Start/Stop ne doivent être appelés qu'une fois (depuis le
	/// même thread). Enqueue* est thread-safe.
	class SqlDelayThread
	{
	public:
		using ExecuteCallback = std::function<void(bool ok)>;

		/// Construit le worker. \p maxQueueSize est la capacité max avant rejet.
		SqlDelayThread(ConnectionPool& pool, size_t maxQueueSize);
		~SqlDelayThread();
		SqlDelayThread(const SqlDelayThread&) = delete;
		SqlDelayThread& operator=(const SqlDelayThread&) = delete;

		/// Démarre le worker. À appeler une fois.
		void Start();

		/// Arrête le worker proprement : drain la queue restante, puis join.
		/// À appeler une fois en fin de vie.
		void Stop();

		/// Enqueue un INSERT/UPDATE/DELETE asynchrone. \p callback peut être null
		/// (fire-and-forget). Retourne false si la queue est pleine (politique
		/// reject, le caller décide).
		bool EnqueueExecute(std::string sql, ExecuteCallback callback);

		/// Taille actuelle de la queue (snapshot, peut bouger immédiatement).
		size_t QueueSize() const;

	private:
		struct Job
		{
			std::string sql;
			ExecuteCallback callback;
		};

		void WorkerLoop();

		ConnectionPool& m_pool;
		const size_t m_maxQueueSize;
		std::thread m_thread;
		std::atomic<bool> m_running{false};
		std::atomic<bool> m_stopRequested{false};
		mutable std::mutex m_mutex;
		std::condition_variable m_cv;
		std::deque<Job> m_queue;
	};
}
```

- [ ] **Step 9.2 : Commit**

```bash
git add engine/server/db/SqlDelayThread.h
git commit -m "feat(server/db): SqlDelayThread header (signature only)

Worker async pour DB hors tick : Start/Stop/EnqueueExecute avec callback
optionnel et queue bornée (politique reject à pleine capacité).

CMANGOS.13 (Phase 1a) — implémentation suit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10 : SqlDelayThread — implémentation

**Files:**
- Create: `engine/server/db/SqlDelayThread.cpp`

- [ ] **Step 10.1 : Implémentation**

```cpp
#include "engine/server/db/SqlDelayThread.h"

#include "engine/server/db/ConnectionPool.h"
#include "engine/server/db/DbHelpers.h"

#include <mysql.h>

namespace engine::server::db
{
	SqlDelayThread::SqlDelayThread(ConnectionPool& pool, size_t maxQueueSize)
		: m_pool(pool)
		, m_maxQueueSize(maxQueueSize)
	{
	}

	SqlDelayThread::~SqlDelayThread()
	{
		// Si l'utilisateur a oublié Stop(), on l'appelle ici pour ne pas crasher.
		if (m_running.load())
			Stop();
	}

	void SqlDelayThread::Start()
	{
		if (m_running.exchange(true))
			return;  // déjà démarré
		m_stopRequested.store(false);
		m_thread = std::thread(&SqlDelayThread::WorkerLoop, this);
	}

	void SqlDelayThread::Stop()
	{
		if (!m_running.load())
			return;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_stopRequested.store(true);
		}
		m_cv.notify_all();
		if (m_thread.joinable())
			m_thread.join();
		m_running.store(false);
	}

	bool SqlDelayThread::EnqueueExecute(std::string sql, ExecuteCallback callback)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_queue.size() >= m_maxQueueSize)
				return false;
			m_queue.push_back(Job{std::move(sql), std::move(callback)});
		}
		m_cv.notify_one();
		return true;
	}

	size_t SqlDelayThread::QueueSize() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_queue.size();
	}

	void SqlDelayThread::WorkerLoop()
	{
		while (true)
		{
			Job job;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cv.wait(lock, [this] {
					return m_stopRequested.load() || !m_queue.empty();
				});
				// Drain : on traite tout ce qui est en queue avant de quitter.
				if (m_queue.empty())
				{
					if (m_stopRequested.load())
						return;
					continue;
				}
				job = std::move(m_queue.front());
				m_queue.pop_front();
			}

			// Exécution hors lock.
			auto guard = m_pool.Acquire();
			MYSQL* mysql = guard.get();
			const bool ok = mysql && DbExecute(mysql, job.sql);
			if (job.callback)
				job.callback(ok);
		}
	}
}
```

- [ ] **Step 10.2 : Build**

```bash
cmake --build --preset linux-x64 --target server_app
```

Expected: **PASS** (le `server_app` n'utilise pas encore SqlDelayThread, donc juste vérification de non-régression).

- [ ] **Step 10.3 : Commit**

```bash
git add engine/server/db/SqlDelayThread.cpp
git commit -m "feat(server/db): SqlDelayThread implementation

Boucle worker : wait CV → pop job → DbExecute hors lock → callback.
Drain à Stop() : on traite tout ce qui reste avant de quitter.
EnqueueExecute reject (false) si queue pleine.

CMANGOS.13 (Phase 1a) — tests dans la tâche suivante.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11 : SqlDelayThread — tests start/enqueue/stop + queue overflow

**Files:**
- Create: `engine/server/db/SqlDelayThreadTests.cpp`
- Modify: `engine/server/CMakeLists.txt`

- [ ] **Step 11.1 : Écrire le test**

```cpp
// CMANGOS.13 (Phase 1a) — Tests SqlDelayThread.

#include "engine/server/db/SqlDelayThread.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
	using engine::server::db::SqlDelayThread;
	using engine::server::db::ConnectionPool;

	bool TestEnqueueAndCallback(ConnectionPool& pool)
	{
		SqlDelayThread worker(pool, 1024);
		worker.Start();

		std::atomic<int> okCount{0};
		std::atomic<int> failCount{0};
		const int N = 5;
		for (int i = 0; i < N; ++i)
		{
			// Requête bénigne : SELECT 1. Pas de side-effect DB.
			worker.EnqueueExecute("DO 1", [&](bool ok) {
				if (ok) okCount.fetch_add(1);
				else failCount.fetch_add(1);
			});
		}

		// Attente complétion (timeout 2s).
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
		while (okCount.load() + failCount.load() < N
			&& std::chrono::steady_clock::now() < deadline)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}

		worker.Stop();

		if (okCount.load() != N)
		{
			LOG_ERROR(Core, "[SqlDelayThreadTests] expected {} OK callbacks, got {} (fail={})",
				N, okCount.load(), failCount.load());
			return false;
		}
		LOG_INFO(Core, "[SqlDelayThreadTests] {} jobs all OK", N);
		return true;
	}

	bool TestQueueOverflow(ConnectionPool& pool)
	{
		// Queue size = 2 pour forcer overflow rapide.
		SqlDelayThread worker(pool, 2);
		// On ne démarre PAS le worker → la queue se remplit sans drain.
		const bool a = worker.EnqueueExecute("DO 1", nullptr);
		const bool b = worker.EnqueueExecute("DO 1", nullptr);
		const bool c = worker.EnqueueExecute("DO 1", nullptr);  // doit être rejeté
		if (!a || !b || c)
		{
			LOG_ERROR(Core, "[SqlDelayThreadTests] expected a=true b=true c=false, got {} {} {}",
				a, b, c);
			return false;
		}
		// On démarre puis on stop pour drain proprement (le destructeur appellera Stop).
		worker.Start();
		// Petite attente pour que la queue se vide.
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		worker.Stop();
		LOG_INFO(Core, "[SqlDelayThreadTests] Queue overflow rejection OK");
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
		LOG_INFO(Core, "[SqlDelayThreadTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		LOG_ERROR(Core, "[SqlDelayThreadTests] Pool Init failed");
		engine::core::Log::Shutdown();
		return 1;
	}

	const bool ok = TestEnqueueAndCallback(pool) && TestQueueOverflow(pool);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
```

- [ ] **Step 11.2 : Ajouter la cible CMake**

Localiser dans `engine/server/CMakeLists.txt` la cible `sql_prepared_statement_tests` (ajoutée à Task 8.2). **Ajouter juste après** :

```cmake
  # CMANGOS.13 (Phase 1a) : SqlDelayThread async worker tests
  add_executable(sql_delay_thread_tests
    db/SqlDelayThreadTests.cpp
    db/SqlDelayThread.cpp
    db/ConnectionPool.cpp
    db/DbHelpers.cpp
  )
  target_include_directories(sql_delay_thread_tests PRIVATE ${CMAKE_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
  target_link_libraries(sql_delay_thread_tests PRIVATE engine_core ${MYSQL_LIBRARY} pthread)
  target_compile_options(sql_delay_thread_tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME sql_delay_thread_tests COMMAND sql_delay_thread_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 11.3 : Build + run**

```bash
cmake --build --preset linux-x64 --target sql_delay_thread_tests
ctest --preset linux-x64 -R sql_delay_thread_tests --output-on-failure
```

Expected: **PASS** — logs `5 jobs all OK` puis `Queue overflow rejection OK`.

- [ ] **Step 11.4 : Commit**

```bash
git add engine/server/db/SqlDelayThreadTests.cpp engine/server/CMakeLists.txt
git commit -m "test(server/db): SqlDelayThread enqueue/callback + queue overflow

Tests :
- Enqueue 5 jobs DO 1 → 5 callbacks OK reçus dans 2s.
- Queue size=2 sans worker démarré → 2 enqueues OK + 1 rejeté (false).

CMANGOS.13 (Phase 1a) — TDD green.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 12 : Documentation finale + dépot config par défaut

**Files:**
- Modify: `docs/db_sql_guidelines.md` (ajouter section Phase 1a)
- Modify: `config.json` (clés par défaut Phase 1a)

- [ ] **Step 12.1 : Vérifier que `docs/db_sql_guidelines.md` existe**

```bash
test -f docs/db_sql_guidelines.md && echo "exists" || echo "missing"
```

Expected: `exists` (référencé dans `DbHelpers.h`).

- [ ] **Step 12.2 : Ajouter la section Phase 1a en fin de fichier**

Localiser la fin de `docs/db_sql_guidelines.md`. **Ajouter à la suite** :

```markdown

## Phase 1a — SQLStorage / SqlPreparedStatement / SqlDelayThread (CMANGOS.13)

Trois utilitaires DB ajoutés dans `engine/server/db/` :

### `SQLStorage<T>` — cache RAM typé read-only

Pour les tables **statiques** consultées en hot path (templates créatures,
items, factions, etc.). Charge une fois au boot via `Load(pool, table, pk,
mapper)`, puis `Find(pk)` est O(1) sans lock.

```cpp
struct CreatureTemplate { uint32_t entry; std::string name; int32_t level; };

SQLStorage<CreatureTemplate> g_creatureTemplates;
g_creatureTemplates.Load(pool, "creature_template", "entry",
    [](char** row) -> CreatureTemplate {
        CreatureTemplate t{};
        t.entry = std::strtoul(row[0], nullptr, 10);
        t.name  = row[1] ? row[1] : "";
        t.level = std::atoi(row[2]);
        return t;
    });

const CreatureTemplate* tmpl = g_creatureTemplates.Find(42);
```

**Convention** : un `SQLStorage` par table statique, instancié comme global
ou membre de `ServerApp`. Pas de hot-reload pour l'instant — refonte du
storage = redéploiement.

### `SqlPreparedStatement` — bindings type-safe + cache LRU

Pour les **queries hot path** avec paramètres. Évite le parsing SQL côté
MySQL à chaque appel.

```cpp
SqlPreparedStatementCache cache(64);  // par worker DB
auto* stmt = cache.Acquire(mysql, "SELECT name FROM accounts WHERE id = ?");
stmt->Bind(0, accountId);
stmt->Execute();
while (stmt->FetchRow())
{
    std::string name = stmt->GetString(0);
}
```

**Convention** : un cache par worker thread (pas de mutex interne). Si
plusieurs threads partagent une connexion, ils partagent un cache —
sériaaliser via mutex applicatif.

### `SqlDelayThread` — worker async pour DB hors tick

Pour les opérations **non-bloquantes** (audit log, save différé). Le tick
n'attend pas la complétion DB.

```cpp
SqlDelayThread worker(pool, 1024);
worker.Start();

worker.EnqueueExecute("INSERT INTO audit (...) VALUES (...)",
    [](bool ok) { if (!ok) LOG_WARN(Core, "audit failed"); });

// fin du process :
worker.Stop();  // drain queue puis join
```

**Politique queue pleine** : `EnqueueExecute` retourne `false`. Le caller
décide (drop, retry, log). Ne **jamais** bloquer le tick en attente de
slot.
```

- [ ] **Step 12.3 : Vérifier qu'il n'y a pas déjà une clé `db.delay_thread_*` dans `config.json`**

```bash
grep -n "delay_thread\|prepared_statement_cache\|sql_storage_log" config.json || echo "no existing keys"
```

Expected: `no existing keys` (sinon, ne pas écraser et ajuster).

- [ ] **Step 12.4 : Ajouter les clés par défaut Phase 1a dans `config.json`**

Localiser dans `config.json` la section `"db": { ... }`. **Ajouter à l'intérieur** (en respectant la virgule de séparation) :

```json
    "delay_thread_enabled": true,
    "delay_thread_queue_size": 1024,
    "prepared_statement_cache_size_per_conn": 64,
    "sql_storage_log_load_durations": true,
```

(Si la section `"db"` n'existe pas, l'ajouter au niveau racine ; si déjà présente sans virgule terminale, ajuster la virgule du dernier champ existant.)

- [ ] **Step 12.5 : Vérifier que le fichier reste un JSON valide**

```bash
python3 -c "import json; json.load(open('config.json'))" && echo "valid JSON" || echo "INVALID JSON"
```

Expected: `valid JSON`.

- [ ] **Step 12.6 : Commit final + mention de redéploiement**

```bash
git add docs/db_sql_guidelines.md config.json
git commit -m "docs(db): Phase 1a SQLStorage/PreparedStatement/DelayThread + config

Documente les 3 utilitaires Phase 1a dans docs/db_sql_guidelines.md avec
exemples d'usage et conventions (un SQLStorage par table statique, un
cache PS par worker, queue reject pour DelayThread).

Ajoute clés par défaut dans config.json :
- db.delay_thread_enabled = true
- db.delay_thread_queue_size = 1024
- db.prepared_statement_cache_size_per_conn = 64
- db.sql_storage_log_load_durations = true

Déploiement : ⚠️ redéploiement serveur (master + shard linux) requis —
nouvelles capacités runtime DB ajoutées (le binaire doit être recompilé,
mais aucun changement de protocole wire). Migration 0041 idempotente.

CMANGOS.13 (Phase 1a) — fin de la phase.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 13 : Validation finale (CI / DoD)

- [ ] **Step 13.1 : Re-run de l'ensemble des tests Phase 1a**

```bash
ctest --preset linux-x64 -R "(sql_storage|sql_prepared_statement|sql_delay_thread)" --output-on-failure
```

Expected: 3 tests **PASS** (ou skip si `db.host` non configuré).

- [ ] **Step 13.2 : Build complet du serveur (vérifier non-régression)**

```bash
cmake --build --preset linux-x64 --target server_app
```

Expected: **PASS** sans warning supplémentaire (`-Wall -Wextra -Wpedantic` actifs sur les nouvelles cibles).

- [ ] **Step 13.3 : Vérifier l'arbre git propre + récap**

```bash
git status
git log --oneline -15
```

Expected:
- working tree clean
- ~12 commits Phase 1a (Task 1 → Task 12) au-dessus du point de départ.

- [ ] **Step 13.4 : Récap DoD final**

Cocher manuellement chaque item :
- [ ] Build linux-x64 OK (Task 13.2 PASS)
- [ ] 3 nouveaux tests PASS (Task 13.1)
- [ ] Migration `0041_phase_1a_test_storage.sql` idempotente
- [ ] `SQLStorage<T>` chargement OK + Find O(1) + double-load rejeté
- [ ] `SqlPreparedStatement` bind/execute/fetch OK + cache LRU OK
- [ ] `SqlDelayThread` enqueue/callback OK + queue overflow rejet OK
- [ ] Aucun nouveau dossier racine non autorisé
- [ ] Doc `docs/db_sql_guidelines.md` mise à jour
- [ ] `config.json` étendu (4 nouvelles clés)
- [ ] Mention redéploiement serveur dans le dernier commit

Si tous les items sont cochés → Phase 1a Database est livrée. **Suite** :
créer le plan Phase 1b Globals/Conditions (qui consommera `SQLStorage`).

---

## Notes pour l'exécutant

### Si le test DB échoue avec "table phase_1a_test_storage doesn't exist"

La migration `0041` n'a pas été appliquée. Soit :
- Lancer le binaire `migration_runner` (s'il existe au chemin standard)
- Soit appliquer manuellement : `mysql -h ... < db/migrations/0041_phase_1a_test_storage.sql`

### Si `MYSQL_BIND::is_null` ne compile pas (type `bool*` vs `my_bool*`)

Selon la version libmysqlclient, le champ peut être `bool*` (récent) ou
`my_bool*` (ancien). La code utilise `reinterpret_cast<bool*>` qui marche
des deux côtés sur les compilateurs modernes. Si erreur, remplacer par
`my_bool*` et ajouter `using my_bool = char;` en début de fichier.

### Si MYSQL_TYPE_LONG/LONGLONG produit des erreurs de signed/unsigned

Le type est tracké explicitement par `m_paramTypes[i]` et `m_paramIsUnsigned[i]`
est set par chaque `Bind(uint*)` overload. L'`Execute()` propage ces valeurs
dans `MYSQL_BIND::is_unsigned`. Si MySQL renvoie quand même une erreur "out
of range" sur des `uint64_t ≥ 2^63`, vérifier que la version libmysqlclient
respecte `is_unsigned` (versions ≥ 5.7).

### Différences vs spec cmangos d'origine

Le ticket source CMANGOS.13 mentionne `SQLStorageDecl.h` (macros pour
déclarer le mapping) et `QueryResultFuture` (pattern future pour les SELECT
async). Le plan substitue :
- **Lambda mapper** au lieu de macros — plus C++20 idiomatique, moins de magic.
- **Callback `bool ok`** au lieu de `std::future<QueryResult>` — pour le
  `SqlDelayThread`, on ne fait que des INSERT/UPDATE/DELETE async (pas de
  SELECT). Si un SELECT async devient nécessaire dans un ticket downstream,
  ajouter `EnqueueQuery(sql, std::function<void(MYSQL_RES*)>)` ou un retour
  `std::future<MYSQL_RES*>`.

Ces choix sont délibérés et plus simples. Le ticket source servait
d'inspiration, pas de spec littérale (cf. CLAUDE.md sur l'adaptation
cmangos LCDLLN).

### Performance baseline attendue

Sur une instance MySQL locale, on attend :
- `SQLStorage::Load` 100 lignes : < 10 ms
- `SqlPreparedStatement::Execute` (statement caché) : < 1 ms
- `SqlDelayThread` 1000 jobs : < 100 ms cumulé (dépend de la concurrence
  pool DB)

Si nettement au-dessus, profiler — peut indiquer une régression du pool
ou du logger.

---

*Plan généré le 2026-05-08 par la skill `superpowers:writing-plans` à
partir des fiches d'audit `docs/superpowers/audits/2026-05-08-cmangos-gap-analysis/CMANGOS.13.md`.*
