#pragma once
// LunarCalendar : calcul stateless de la phase lunaire courante a partir
// d'un timestamp Unix. 16 phases (0..15), cycle de 14 jours reels.
// Header-only, deterministe, partage par master et shardd.
//
// Conventions :
//   - phase 0  : NewMoon (illumination ~0)
//   - phase 7  : FullMoon (illumination 1.0)
//   - phase 15 : Earthshine late (~0.04, juste avant un nouveau cycle)
//
// Le calcul est purement deterministe : aucune dependance a un etat
// mutable. La meme entree (realNowMs, cycleStartMs, cycleDurationMs)
// produira toujours la meme phase et illumination, ce qui permet au
// master et au shardd de calculer la phase localement sans
// synchronisation.

#include <cstdint>
#include <cmath>

namespace engine::server::world
{
	/// Une phase lunaire complete avec son indice (0..15) et son
	/// illumination (0..1, fraction eclairee).
	struct LunarPhaseInfo
	{
		uint8_t phase        = 0;     ///< Index 0..15
		float   illumination = 0.0f;  ///< 0..1, fraction eclairee
	};

	/// Cycle complet en millisecondes : 14 jours * 24h * 3600s * 1000ms.
	inline constexpr uint64_t kDefaultLunarCycleMs = 14ull * 24ull * 3600ull * 1000ull;

	/// Nombre de phases dans le cycle.
	inline constexpr uint8_t kLunarPhaseCount = 16u;

	class LunarCalendar
	{
	public:
		/// Calcule la phase lunaire pour un timestamp donne.
		/// \param realNowMs       Timestamp Unix courant en ms.
		/// \param cycleStartMs    Timestamp Unix du debut du cycle de reference.
		/// \param cycleDurationMs Duree d'un cycle complet en ms.
		/// \return Phase + illumination. Si \p cycleDurationMs == 0 retourne {0, 0}.
		///         Si \p realNowMs < \p cycleStartMs, retourne phase 0 (clamp anti-underflow).
		static LunarPhaseInfo Compute(uint64_t realNowMs,
		                              uint64_t cycleStartMs,
		                              uint64_t cycleDurationMs)
		{
			if (cycleDurationMs == 0) return {};
			const uint64_t elapsed = (realNowMs >= cycleStartMs) ? (realNowMs - cycleStartMs) : 0ull;
			const uint64_t cyclePos = elapsed % cycleDurationMs;
			const uint64_t phaseDurationMs = cycleDurationMs / kLunarPhaseCount;
			uint8_t phase = static_cast<uint8_t>(cyclePos / phaseDurationMs);
			if (phase >= kLunarPhaseCount) phase = kLunarPhaseCount - 1u;
			return { phase, ComputeIllumination(phase) };
		}

		/// Calcule l'illumination (0..1) pour un index de phase.
		/// Sinusoide centree sur Full Moon (index 7) :
		///   - phase 0  -> 0.0  (NewMoon)
		///   - phase 7  -> 1.0  (FullMoon)
		///   - phase 15 -> ~0.04 (Earthshine late, tres sombre)
		/// \param phase Index 0..15. Au-dela, le resultat est wrap par la sinusoide.
		static float ComputeIllumination(uint8_t phase)
		{
			constexpr float kPi = 3.14159265358979323846f;
			const float t = (static_cast<float>(phase) - 7.0f) * (kPi / 8.0f);
			return 0.5f * (1.0f + std::cos(t));
		}

		/// Retourne le timestamp ms du prochain changement de phase.
		/// Utile pour l'UI ("prochaine phase dans XXh") et pour le tick
		/// du LunarHandler qui peut comparer realNowMs >= NextChangeTsMs.
		/// \param realNowMs       Timestamp Unix courant en ms.
		/// \param cycleStartMs    Timestamp Unix du debut du cycle de reference.
		/// \param cycleDurationMs Duree d'un cycle complet en ms.
		/// \return Timestamp ms absolu du debut de la phase suivante. Si
		///         \p cycleDurationMs == 0 retourne \p realNowMs (no-op).
		static uint64_t NextChangeTsMs(uint64_t realNowMs,
		                               uint64_t cycleStartMs,
		                               uint64_t cycleDurationMs)
		{
			if (cycleDurationMs == 0) return realNowMs;
			const uint64_t elapsed = (realNowMs >= cycleStartMs) ? (realNowMs - cycleStartMs) : 0ull;
			const uint64_t cyclePos = elapsed % cycleDurationMs;
			const uint64_t phaseDurationMs = cycleDurationMs / kLunarPhaseCount;
			const uint64_t currentPhaseStartMs = (cyclePos / phaseDurationMs) * phaseDurationMs;
			const uint64_t nextPhaseStartMs = currentPhaseStartMs + phaseDurationMs;
			const uint64_t cyclesElapsed = elapsed / cycleDurationMs;
			return cycleStartMs + (cyclesElapsed * cycleDurationMs) + nextPhaseStartMs;
		}
	};
}
