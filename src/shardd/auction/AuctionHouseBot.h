#pragma once
// CMANGOS.44 (Phase 5.44a) — AuctionHouseBot : alimente l'hotel des
// ventes en items/encheres simulees pour serveur low-pop. Header-only.
//
// Activation conditionnelle (config) — dort si la pop est suffisante.
// Genere des listings deterministes (seed-based) pour faciliter tests.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace engine::server::auction
{
	using ItemTemplateId = uint32_t;

	struct AhBotListing
	{
		ItemTemplateId  itemTemplateId;
		uint32_t        count;
		uint64_t        startBidCopper;
		uint64_t        buyoutCopper;     ///< 0 = pas de buyout
		uint64_t        durationMs;       ///< 12h, 24h, 48h
	};

	struct AhBotConfig
	{
		uint32_t targetActiveListings = 50;  ///< cible totale d'encheres actives
		uint32_t maxListingsPerTick   = 5;   ///< rate-limit par tick (eviter spam)
		uint64_t seed                 = 0;
	};

	class AuctionHouseBot
	{
	public:
		explicit AuctionHouseBot(AhBotConfig cfg)
			: m_cfg(cfg), m_rng(cfg.seed) {}

		/// Genere jusqu'a \p missing nouveaux listings (clamp a maxListingsPerTick).
		std::vector<AhBotListing> Tick(uint32_t currentActive)
		{
			std::vector<AhBotListing> out;
			if (currentActive >= m_cfg.targetActiveListings) return out;

			uint32_t deficit = m_cfg.targetActiveListings - currentActive;
			if (deficit > m_cfg.maxListingsPerTick) deficit = m_cfg.maxListingsPerTick;

			std::uniform_int_distribution<uint32_t> itemDist(1, 10000);
			std::uniform_int_distribution<uint32_t> countDist(1, 5);
			std::uniform_int_distribution<uint64_t> bidDist(1, 100);
			for (uint32_t i = 0; i < deficit; ++i)
			{
				AhBotListing l;
				l.itemTemplateId = itemDist(m_rng);
				l.count          = countDist(m_rng);
				l.startBidCopper = bidDist(m_rng) * 10000ull;        // 1-100 silver
				l.buyoutCopper   = l.startBidCopper * 3;
				l.durationMs     = 24ull * 3600ull * 1000ull;        // 24h
				out.push_back(l);
			}
			return out;
		}

		const AhBotConfig& Config() const { return m_cfg; }

	private:
		AhBotConfig    m_cfg;
		std::mt19937_64 m_rng;
	};
}
