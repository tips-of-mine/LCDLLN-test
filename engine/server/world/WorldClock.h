#pragma once
// CMANGOS.28 (Phase 3.28a) — WorldClock : horloge serveur (game time, day/night
// cycle, speed multiplier admin) et utilities derives. Header-only.

#include <cstdint>
#include <algorithm>

namespace engine::server::world
{
	/// Phase de la journee selon l'heure (heures locales 0..23).
	enum class DayPhase : uint8_t { Night, Dawn, Day, Dusk };

	class WorldClock
	{
	public:
		/// \param epochMs   debut du temps serveur (timestamp ms)
		/// \param speed     multiplicateur (1.0 = realtime, 60.0 = 1h reel = 60h jeu)
		WorldClock(uint64_t epochMs, double speed)
			: m_epochMs(epochMs), m_speed(speed) {}

		/// Temps en jeu ecoule depuis epoch en ms.
		uint64_t GameTimeMs(uint64_t realNowMs) const
		{
			if (realNowMs <= m_epochMs) return 0;
			return static_cast<uint64_t>(static_cast<double>(realNowMs - m_epochMs) * m_speed);
		}

		/// Heure en jeu modulo 24h, en heures (0..23).
		uint8_t HourOfDay(uint64_t realNowMs) const
		{
			const uint64_t gameMs = GameTimeMs(realNowMs);
			const uint64_t hourMs = 60ull * 60ull * 1000ull;
			return static_cast<uint8_t>((gameMs / hourMs) % 24);
		}

		DayPhase Phase(uint64_t realNowMs) const
		{
			const uint8_t h = HourOfDay(realNowMs);
			if (h >= 22 || h < 5)  return DayPhase::Night;
			if (h < 7)             return DayPhase::Dawn;
			if (h < 19)            return DayPhase::Day;
			return DayPhase::Dusk; // 19..21
		}

		double Speed() const { return m_speed; }
		void   SetSpeed(double s) { m_speed = std::max(0.0, s); }

	private:
		uint64_t m_epochMs;
		double   m_speed;
	};
}
