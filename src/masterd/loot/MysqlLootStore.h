#pragma once
// Wave 5 Persistence (Phase 3.17b) - MysqlLootStore : wrapper MySQL
// pour persister les tables de loot (definition + entries). Migration
// 0057_loot_tables.sql. Cible UNIX (master).
//
// Read-only V1 : les tables sont seedees via INSERT IGNORE dans la
// migration (loup_base + lapin_base). Le LootHandler appelle
// LoadAllTables au boot pour preparer une future integration avec un
// systeme de drop par creature (out-of-scope de cette PR cote wire).
//
// Lifecycle :
//   - main_linux : instancie le store -> lootHandler.SetLootStore.
//   - Pas d'appel obligatoire au boot V1 (le LootHandler garde son
//     tableau hardcode 5 items pour SimulateRoll). Une PR ulterieure
//     basculera le LootHandler sur les tables DB pour les vrais drops.
//   - Pas de mutation V1 : la table est read-only cote master, edition
//     reservee aux migrations / outils d'admin externes.

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::loot_db
{
	/// Ligne table de loot (definition).
	struct LootTableRow
	{
		uint32_t    tableId      = 0;
		std::string name;
		std::string description;
	};

	/// Ligne entree de table de loot (item drop possible).
	struct LootEntryRow
	{
		uint32_t    entryId          = 0;
		uint32_t    tableId          = 0;
		uint32_t    itemTemplateId   = 0;
		std::string itemName;
		uint32_t    dropChancePct    = 0;
		uint32_t    minCount         = 1;
		uint32_t    maxCount         = 1;
	};

	/// MySQL backed store pour LootHandler. Toutes les operations
	/// retournent une collection vide si le pool n'est pas initialise
	/// (le caller en deduit "fallback in-memory tableau hardcode").
	class MysqlLootStore final
	{
	public:
		explicit MysqlLootStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Retourne true si le store est en mode DB (pool initialise).
		bool IsAvailable() const noexcept;

		/// Charge toutes les tables de loot (sans leurs entries). Pour
		/// recuperer les entries d'une table, appeler LoadEntriesForTable.
		/// Vide si DB indisponible ou table vide.
		std::vector<LootTableRow> LoadAllTables() const;

		/// Charge les entries (items dropables) d'une table donnee.
		/// Vide si DB indisponible, table inexistante, ou aucune entry.
		///
		/// \param tableId id de la loot_tables ligne.
		std::vector<LootEntryRow> LoadEntriesForTable(uint32_t tableId) const;

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
