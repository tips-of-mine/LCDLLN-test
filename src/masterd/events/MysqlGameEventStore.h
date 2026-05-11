#pragma once
// Wave 5 Persistence (Phase 5.31b) - MysqlGameEventStore : charge la
// definition des events saisonniers depuis MySQL. Migration 0051.
// Read-only (les events sont seedees par migration via INSERT IGNORE ;
// l'edition cote admin est out-of-scope de cette PR).
//
// Le GameEventHandler appelle LoadAll au boot ; si la table est vide
// ou la DB indisponible, il retombe sur le seed hardcode.

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::events
{
	/// Ligne d'event telle que persistee. Aligne sur GameEventDef
	/// (a l'eventId / name / start / duration / recur / lunarMask pres).
	struct GameEventRow
	{
		uint32_t    eventId                  = 0;
		std::string name;
		uint64_t    startTsMs                = 0;
		uint64_t    durationMs               = 0;
		uint64_t    recurMs                  = 0;
		uint16_t    requiresLunarPhaseMask   = 0xFFFFu;
	};

	class MysqlGameEventStore final
	{
	public:
		explicit MysqlGameEventStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Retourne true si la DB est disponible.
		bool IsAvailable() const noexcept;

		/// Charge tous les events. Vide si DB indisponible ou table vide.
		std::vector<GameEventRow> LoadAll() const;

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
