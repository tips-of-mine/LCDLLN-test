#pragma once
// Wave 9 — Wrapper runtime AntiCheatGameplay : detient un detecteur
// AntiCheatGameplay seede au boot du shardd avec une config V1 hardcodee
// (max speed 7.5 m/s * tolerance 1.5 = 11.25 m/s effectif, max single
// step 50m -> teleport hack au-dela). Tick periodique (1s) qui scan une
// poignee de "fake players" pour exercer le path CheckMovement -> verdict
// au runtime. Future iteration : SeedFromConfig() qui lit les seuils
// depuis config.json, et un fan-out reel via PlayerManager au lieu de
// players fictifs.
//
// Le but de cette PR : prouver que le path tick AntiCheat est reellement
// exerce a chaque seconde, pas seulement teste en unit. Les "violations"
// detectees sont cumulees et loggees toutes les 60s ; un cumul a 0 ne
// logge rien (pas de bruit en prod si tout va bien).

#include "src/shardd/anticheat/AntiCheatGameplay.h"

#include <cstddef>
#include <cstdint>

namespace engine::server::anticheat
{
	/// Wrapper minimaliste autour de AntiCheatGameplay : detient le
	/// detecteur configure V1 (maxSpeedMps=7.5, tolerance=1.5,
	/// maxSingleStepM=50.0) et expose un Tick() qui scan une petite
	/// liste de PlayerIds fictifs. Future iteration : etat partage avec
	/// PlayerManager + EventBus pour le fan-out reel des MOVE_REPORT.
	class AntiCheatGameplayRuntime
	{
	public:
		AntiCheatGameplayRuntime() = default;

		/// Charge la config V1 hardcodee dans le detecteur interne :
		///   - maxSpeedMps    = 7.5  (walk + run + mount typique)
		///   - speedTolerance = 1.5  (50% lag headroom)
		///   - maxSingleStepM = 50.0 (teleport hack au-dela)
		/// Idempotent : peut etre rappele pour reset l'etat interne du
		/// detecteur (utile en tests, jamais en prod).
		void SeedV1Config();

		/// Scan une liste fixe de PlayerIds fictifs (1001..1003) et fait
		/// avancer la position de chacun d'une petite quantite plausible
		/// depuis le dernier Tick. Retourne le nombre de verdicts != OK
		/// detectes ce tick (typiquement 0 en regime nominal).
		///
		/// V1 : positions deterministes proches de la realite (deplacement
		/// "lent" 1m/s). En pratique, le detecteur n'a rien a flag tant que
		/// SeedV1Config() est applique tel quel ; ce Tick() prouve juste
		/// que le path CheckMovement est exerce a chaque seconde au runtime.
		///
		/// \param nowMs Horloge wall-clock en millisecondes (system_clock
		///   typiquement). Doit etre monotone d'un tick a l'autre, sinon
		///   le calcul de delta-temps interne donnera des speeds aberrants.
		std::size_t Tick(std::uint64_t nowMs);

		/// Nombre total de violations detectees depuis le boot (cumul
		/// sur toute la duree de vie du runtime). Sert pour le log
		/// periodique 60s "[AntiCheat] N violations in last 60s".
		std::uint64_t TotalViolations() const noexcept { return m_totalViolations; }

	private:
		AntiCheatGameplay m_detector;
		std::uint64_t     m_totalViolations = 0;
		std::uint64_t     m_tickCounter     = 0;   ///< Pour generer un mouvement deterministe.
	};
}
