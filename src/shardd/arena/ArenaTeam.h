#pragma once
// CMANGOS.08 (Phase 5.08a) — ArenaTeam : equipe d'arene 2v2/3v3/5v5 avec
// rating ELO + match history. Header-only.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>

namespace engine::server::arena
{
	using TeamId    = uint32_t;
	using ToonGuid  = uint64_t;

	enum class TeamSize : uint8_t { v2 = 2, v3 = 3, v5 = 5 };

	struct ArenaTeam
	{
		TeamId   id          = 0;
		TeamSize size        = TeamSize::v2;
		std::string name;
		std::vector<ToonGuid> roster;
		uint32_t rating       = 1500; ///< ELO de depart standard
		uint32_t weeklyGames  = 0;
		uint32_t weeklyWins   = 0;
		uint32_t seasonGames  = 0;
		uint32_t seasonWins   = 0;
	};

	/// Calcule la nouvelle paire de ratings ELO apres un match.
	/// \param k        K-factor (typiquement 32 dans WoW arena)
	/// \param winner   nouveau rating du gagnant (sortie)
	/// \param loser    nouveau rating du perdant (sortie)
	inline void ApplyEloUpdate(uint32_t winnerRating, uint32_t loserRating, uint32_t k,
	                            uint32_t& winner, uint32_t& loser)
	{
		const double rW = static_cast<double>(winnerRating);
		const double rL = static_cast<double>(loserRating);
		const double expectedW = 1.0 / (1.0 + std::pow(10.0, (rL - rW) / 400.0));
		const double expectedL = 1.0 - expectedW;
		const double newW = rW + k * (1.0 - expectedW);
		const double newL = rL + k * (0.0 - expectedL);
		// Floor a 0 pour eviter underflow uint32 si match catastrophique.
		winner = static_cast<uint32_t>(std::max(0.0, newW));
		loser  = static_cast<uint32_t>(std::max(0.0, newL));
	}

	/// Registry simple en memoire pour tests/donnees seed.
	class ArenaTeamRegistry
	{
	public:
		void AddTeam(const ArenaTeam& t) { m_teams[t.id] = t; }
		ArenaTeam* Get(TeamId id)
		{
			auto it = m_teams.find(id);
			return (it == m_teams.end()) ? nullptr : &it->second;
		}

		/// Enregistre un match : met a jour ratings (ELO) + stats hebdo/saison.
		/// Retourne true si les 2 equipes existent.
		bool RecordMatch(TeamId winnerId, TeamId loserId, uint32_t k = 32)
		{
			auto* w = Get(winnerId);
			auto* l = Get(loserId);
			if (!w || !l) return false;
			uint32_t newW, newL;
			ApplyEloUpdate(w->rating, l->rating, k, newW, newL);
			w->rating = newW;
			l->rating = newL;
			w->weeklyGames++; w->seasonGames++;
			w->weeklyWins++;  w->seasonWins++;
			l->weeklyGames++; l->seasonGames++;
			return true;
		}

		/// Reset compteurs hebdo (lance le lundi serveur en theorie).
		void ResetWeekly()
		{
			for (auto& [_, t] : m_teams)
			{
				t.weeklyGames = 0;
				t.weeklyWins  = 0;
			}
		}

	private:
		std::unordered_map<TeamId, ArenaTeam> m_teams;
	};
}
