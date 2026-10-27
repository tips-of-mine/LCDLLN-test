#pragma once
// ETAPE4.5 (chantier 2.5 du rapport City of Heroes) — délestage du shard avec hystérésis.
//
// Quand le tick du shard prend trop de temps, le serveur se DÉGRADE plutôt que de
// prendre du retard : moins d'entités envoyées par client, plus de nouveaux PNJ
// d'ambiance, IA d'ambiance et de repos ralenties. Le déclencheur est la DURÉE DU TICK
// (part du budget 1/tickHz réellement consommée, lissée), pas un nombre de joueurs.
//
// Hystérésis :
//   - entrée  : charge lissée ≥ enterLoad (85 %) pendant enterHoldSeconds (2 s) ;
//   - sortie  : charge lissée ≤ exitLoad (60 %) pendant exitHoldSeconds (10 s) ;
//   - jamais deux bascules à moins de minDwellSeconds (15 s) d'intervalle.
// Deux seuils distincts et des durées de maintien : pas d'oscillation autour d'un seuil.
//
// Module PUR (horloge passée en argument, en secondes) : testé seul
// (load_shedding_tests). Pas thread-safe : thread de tick.

#include <cstdint>

namespace engine::server
{
	/// Réglages (config server.load_shedding.*), bornés par Sanitize.
	struct LoadSheddingConfig
	{
		bool enabled = true;           ///< server.load_shedding.enabled
		double enterLoad = 0.85;       ///< part du budget du tick (1 = budget entier)
		double exitLoad = 0.60;        ///< < enterLoad
		double enterHoldSeconds = 2.0;
		double exitHoldSeconds = 10.0;
		double minDwellSeconds = 15.0;
		double smoothing = 0.1;        ///< poids d'un tick dans la moyenne lissée ∈ ]0, 1]
	};

	/// Borne les réglages (seuils dans [0,05 ; 10], sortie < entrée, durées dans
	/// [0 ; 600] s, lissage dans [0,001 ; 1]).
	LoadSheddingConfig SanitizeLoadShedding(LoadSheddingConfig config);

	/// Transition produite par LoadShedder::OnTick.
	enum class SheddingTransition : uint8_t { None = 0, Entered, Exited };

	class LoadShedder final
	{
	public:
		explicit LoadShedder(LoadSheddingConfig config = {});

		void Configure(LoadSheddingConfig config);
		const LoadSheddingConfig& Config() const { return m_config; }

		/// Un tick vient de se terminer : il a duré \p tickSeconds pour un budget de
		/// \p budgetSeconds (1/tickHz), à l'instant \p nowSeconds (horloge monotone).
		/// \return la bascule éventuelle (une au plus par appel).
		SheddingTransition OnTick(double tickSeconds, double budgetSeconds, double nowSeconds);

		/// Vrai pendant le délestage.
		bool Active() const { return m_active; }
		/// Charge lissée courante (part du budget).
		double SmoothedLoad() const { return m_smoothed; }
		/// Nombre de bascules en délestage depuis le démarrage (télémétrie).
		uint64_t EnterCount() const { return m_enterCount; }

	private:
		LoadSheddingConfig m_config{};
		bool m_active = false;
		bool m_hasSample = false;
		double m_smoothed = 0.0;
		double m_aboveSince = -1.0;   ///< début de la période au-dessus du seuil d'entrée (−1 = non)
		double m_belowSince = -1.0;   ///< début de la période sous le seuil de sortie (−1 = non)
		double m_lastSwitch = -1.0e18;
		uint64_t m_enterCount = 0;
	};
}
