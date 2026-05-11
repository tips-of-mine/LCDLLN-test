#pragma once
// Wave 5 Persistence (Phase 5.21b) - MysqlArenaStore : persiste les
// teams d'arene avec leur progression ELO + stats hebdo/saison.
// Migration 0052_arena_teams.sql. Cible UNIX (master).
//
// Convention V1 : la cle composite (account_id_owner, team_id) reflete
// le fait que le starter set du handler reutilise les ids 1/2/3 par
// account. Quand un team_id global AUTO_INCREMENT sera introduit, la
// signature evoluera.
//
// Lifecycle :
//   - LoadForAccount(accountId) appele a la 1ere TeamList par account
//     (en lieu du SeedStarterTeamsIfNeeded si la DB renvoie des rows).
//   - Upsert(row) appele apres RecordMatch ou seed initial.

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::arena
{
	/// Ligne d'arena team persistee.
	struct ArenaTeamRow
	{
		uint32_t    teamId          = 0;
		uint64_t    accountIdOwner  = 0;
		std::string name;
		uint8_t     size            = 0;
		uint32_t    rating          = 0;
		uint32_t    weeklyGames     = 0;
		uint32_t    weeklyWins      = 0;
		uint32_t    seasonGames     = 0;
		uint32_t    seasonWins      = 0;
	};

	class MysqlArenaStore final
	{
	public:
		explicit MysqlArenaStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		bool IsAvailable() const noexcept;

		/// Charge toutes les teams d'un account (typiquement appele a la
		/// 1ere TeamListRequest). Vide si DB indisponible ou aucune team
		/// persistee pour cet account.
		std::vector<ArenaTeamRow> LoadForAccount(uint64_t accountId) const;

		/// Upsert d'une team (INSERT ... ON DUPLICATE KEY UPDATE). Appele
		/// au seed initial ET apres chaque RecordMatch pour persister le
		/// nouveau rating + stats.
		bool Upsert(const ArenaTeamRow& row);

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
