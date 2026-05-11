#include "src/shardd/anticheat/AntiCheatGameplayRuntime.h"

namespace engine::server::anticheat
{
	namespace
	{
		/// PlayerIds fictifs scannes a chaque Tick. V1 : 3 entrees pour
		/// avoir un fan-out non trivial sans alourdir le path CPU. Future
		/// iteration : remplace par un fan-out reel via PlayerManager.
		constexpr PlayerId kFakePlayers[] = { 1001ULL, 1002ULL, 1003ULL };
	}

	/// Reconstruit le detecteur interne avec la config V1 hardcodee
	/// (cf. AntiCheatGameplayRuntime.h pour les valeurs detaillees). Reset
	/// aussi les compteurs cumulatifs et le tick counter, donc l'effet
	/// est equivalent a une nouvelle instance.
	void AntiCheatGameplayRuntime::SeedV1Config()
	{
		AntiCheatConfig cfg;
		cfg.maxSpeedMps    = 7.5f;
		cfg.speedTolerance = 1.5f;
		cfg.maxSingleStepM = 50.0f;
		m_detector = AntiCheatGameplay(cfg);
		m_totalViolations = 0;
		m_tickCounter     = 0;
	}

	/// Avance chaque PlayerId fictif d'un petit pas plausible (1m sur l'axe
	/// X par tick) puis interroge le detecteur. Le delta-temps theorique
	/// entre deux ticks est de 1000 ms (cadence du wiring main_linux.cpp),
	/// donc la vitesse calculee restera proche de 1 m/s, bien sous le
	/// seuil maxAllowed = 7.5 * 1.5 = 11.25 m/s. En regime nominal, le
	/// retour est donc 0.
	std::size_t AntiCheatGameplayRuntime::Tick(std::uint64_t nowMs)
	{
		++m_tickCounter;
		const float x = static_cast<float>(m_tickCounter) * 1.0f;
		const float y = 0.0f;
		const float z = 0.0f;

		std::size_t violations = 0;
		for (PlayerId pid : kFakePlayers)
		{
			const CheatVerdict v = m_detector.CheckMovement(pid, x, y, z, nowMs);
			if (v != CheatVerdict::OK)
				++violations;
		}
		m_totalViolations += violations;
		return violations;
	}
}
