#pragma once
// Wave 5 Persistence (Phase 5.36b) - MysqlOutdoorPvpStore : persiste
// l'etat des objectifs (owner + capturePct + capturingBy) et les scores
// par zone/faction. Migrations 0054_outdoor_pvp_state.sql. Cible UNIX.
//
// Lifecycle :
//   - LoadStates / LoadScores appeles au boot par OutdoorPvpHandler::SeedV1Zones
//     pour reconstruire le state apres reseed des zones par defaut.
//   - UpsertObjective / UpsertScore appeles a chaque TickCapture qui clos
//     une capture (transition d'owner + score ++).

#include <cstdint>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::outdoorpvp_db
{
	struct ObjectiveRow
	{
		uint32_t zoneId      = 0;
		uint32_t objectiveId = 0;
		uint8_t  owner       = 0xFFu;
		uint32_t capturePct  = 0u;
		uint8_t  capturingBy = 0xFFu;
	};

	struct ScoreRow
	{
		uint32_t zoneId  = 0;
		uint8_t  faction = 0u;
		uint32_t score   = 0u;
	};

	class MysqlOutdoorPvpStore final
	{
	public:
		explicit MysqlOutdoorPvpStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		bool IsAvailable() const noexcept;

		/// Charge toutes les lignes outdoor_pvp_state. Vide si DB indisponible.
		std::vector<ObjectiveRow> LoadStates() const;

		/// Charge toutes les lignes outdoor_pvp_scores. Vide si DB indisponible.
		std::vector<ScoreRow> LoadScores() const;

		/// Upsert d'un objective (owner / capturePct / capturingBy).
		bool UpsertObjective(const ObjectiveRow& row);

		/// Upsert d'un score (zone, faction, score).
		bool UpsertScore(const ScoreRow& row);

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
