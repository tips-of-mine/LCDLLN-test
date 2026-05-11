#pragma once
// Wave 5 Persistence (Phase 5.10b) - MysqlBattleGroundStore : wrapper
// MySQL pour persister l'historique des matchs BG joues. Migration
// 0056_bg_history.sql. Cible UNIX (master).
//
// Le runtime continue de manipuler des ActiveMatch in-memory (steady_clock
// pour le startedAt). A chaque MatchEnd push, le BattleGroundHandler appelle
// InsertMatch pour archiver le resultat. Le matchId in-memory (compteur
// atomic transient) est distinct du match_id DB (auto-increment).
//
// Lifecycle :
//   - main_linux : instancie le store -> bgHandler.SetMatchHistoryStore.
//   - Pas de seed / LoadAll au boot : la table est en write-only V1
//     (l'API LoadRecent est expose pour de futures UI / leaderboard
//     mais pas branchee en lecture par le handler V1).
//   - HandleQueue (post-MatchEnd) : InsertMatch best-effort, echec
//     logge mais n'interrompt pas la sequence wire.
//
// Tous les appels au store sont best-effort : un retour false / 0
// logge un warning mais n'interrompt pas le push MatchEnd au client.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::bg_db
{
	/// Ligne d'historique de match BG.
	struct MatchHistoryRow
	{
		uint64_t    matchId            = 0;
		uint16_t    bgType             = 0;
		std::string mapName;
		uint32_t    allianceScore      = 0;
		uint32_t    hordeScore         = 0;
		uint8_t     winnerFaction      = 2u;  // 0=Alliance, 1=Horde, 2=Draw.
		uint32_t    durationSec        = 0;
		uint64_t    startedAtUnixMs    = 0;
	};

	/// MySQL backed store pour BattleGroundHandler. Toutes les operations
	/// retournent false / vide si le pool n'est pas initialise.
	class MysqlBattleGroundStore final
	{
	public:
		explicit MysqlBattleGroundStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Retourne true si le store est en mode DB (pool initialise).
		bool IsAvailable() const noexcept;

		/// Insere une ligne d'historique de match. Le match_id DB est
		/// auto-genere (AUTO_INCREMENT) -- distinct du matchId in-memory
		/// passe en parametre.
		///
		/// \param bgType            type de BG (1=Warsong, 2=Arathi, 3=Alterac).
		/// \param mapName           nom de la map (UTF-8, <= 64).
		/// \param allianceScore     score final cote Alliance.
		/// \param hordeScore        score final cote Horde.
		/// \param winnerFaction     0=Alliance, 1=Horde, 2=Draw.
		/// \param durationSec       duree totale du match en secondes.
		/// \param startedAtUnixMs   timestamp de creation du match (system_clock ms).
		/// \return match_id DB auto-genere (>0) ou 0 en cas d'echec.
		uint64_t InsertMatch(uint16_t bgType, std::string_view mapName,
			uint32_t allianceScore, uint32_t hordeScore,
			uint8_t winnerFaction, uint32_t durationSec,
			uint64_t startedAtUnixMs);

		/// Charge les N derniers matchs (par started_at desc) pour une
		/// future UI leaderboard. Vide si DB indisponible ou table vide.
		///
		/// \param limit nombre max de lignes a retourner (defaut 50).
		std::vector<MatchHistoryRow> LoadRecent(size_t limit = 50u) const;

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
