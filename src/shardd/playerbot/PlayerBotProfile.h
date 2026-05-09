#pragma once
// CMANGOS.38 (Phase 5.38a) — PlayerBotProfile : profil de bot (classe, role,
// AI tier, equipement seed) + scheduler de spawn pour repeupler les zones
// peu peuplees. Header-only.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server::playerbot
{
	using BotId = uint64_t;

	enum class BotClass : uint8_t
	{
		Warrior = 0, Paladin, Hunter, Rogue, Priest, Shaman, Mage, Warlock, Druid
	};

	enum class BotRole : uint8_t { Tank, Heal, MeleeDps, RangedDps };

	/// Profil statique d'un bot. Sert de seed pour la generation runtime.
	struct PlayerBotProfile
	{
		BotId    id        = 0;
		std::string name;
		BotClass cls       = BotClass::Warrior;
		BotRole  role      = BotRole::MeleeDps;
		uint8_t  level     = 1;
		uint16_t aiTier    = 0;     ///< 0 = naif, 1 = standard, 2 = avance
		uint32_t zoneId    = 0;     ///< zone preferee de spawn
	};

	/// Scheduler simple : retourne les bots qui doivent spawn pour atteindre
	/// la densite cible d'une zone. Politique deterministe pour tests.
	class PlayerBotScheduler
	{
	public:
		void RegisterBot(const PlayerBotProfile& p) { m_bots[p.id] = p; }

		/// Selectionne jusqu'a \p needed bots dans la zone \p zoneId qui ne sont
		/// pas dans \p alreadySpawned. Retourne les ids dans l'ordre d'insertion
		/// (deterministe).
		std::vector<BotId> Pick(uint32_t zoneId, size_t needed,
		                         const std::vector<BotId>& alreadySpawned) const
		{
			std::vector<BotId> out;
			for (const auto& [id, p] : m_bots)
			{
				if (out.size() >= needed) break;
				if (p.zoneId != zoneId) continue;
				bool already = false;
				for (auto s : alreadySpawned) if (s == id) { already = true; break; }
				if (already) continue;
				out.push_back(id);
			}
			return out;
		}

		size_t BotCount() const { return m_bots.size(); }
		const PlayerBotProfile* Get(BotId id) const
		{
			auto it = m_bots.find(id);
			return (it == m_bots.end()) ? nullptr : &it->second;
		}

	private:
		std::unordered_map<BotId, PlayerBotProfile> m_bots;
	};
}
