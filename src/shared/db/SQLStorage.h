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

#include <mysql.h>

// Inclusion directe de ConnectionPool.h car l'implémentation templated de
// Load() utilise pool.Acquire() dans son body — une simple forward declaration
// déclenche un warning [-Wpedantic] sur GCC ("invalid use of incomplete type")
// au moment de l'instanciation par les consommateurs.
#include "engine/server/db/ConnectionPool.h"

namespace engine::server::db
{
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
}
