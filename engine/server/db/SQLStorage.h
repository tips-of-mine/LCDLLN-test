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
