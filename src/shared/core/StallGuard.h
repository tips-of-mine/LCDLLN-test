#pragma once
// ETAPE5.2 (chantier 1.5 du rapport City of Heroes) — ne pas juger les autres sur un
// tour de boucle où l'on a soi-même calé.
//
// Si une boucle de service (boucle principale du master, tick du shard) reste bloquée
// plusieurs secondes — tick très lent, swap, pause de la machine virtuelle, disque gelé —
// les heartbeats et paquets des autres arrivent bien, mais ne sont traités qu'après le
// réveil. Les contrôles de délai (« plus de nouvelles depuis 90 s ⇒ mort ») qui tournent
// à ce moment-là condamneraient à tort des shards ou des clients vivants.
//
// StallGuard mesure l'écart entre deux tours consécutifs. Un écart ≥ stallSeconds (3 s)
// est un calage : les contrôles de délai sont suspendus pendant graceSeconds (30 s), le
// temps que les messages en attente soient traités et que les horodatages se rafraîchissent.
//
// Module PUR (horloge monotone passée en argument, en secondes) : testé seul
// (stall_guard_tests). Pas thread-safe : un StallGuard par boucle.

#include <cstdint>

namespace engine::core
{
	/// Réglages (config <service>.stall_guard.*), bornés par SanitizeStallGuard.
	struct StallGuardConfig
	{
		bool enabled = true;
		double stallSeconds = 3.0;  ///< écart entre deux tours considéré comme un calage
		double graceSeconds = 30.0; ///< suspension des contrôles de délai après un calage
	};

	/// Borne les réglages : calage dans [0,2 ; 600] s, suspension dans [0 ; 3600] s.
	StallGuardConfig SanitizeStallGuard(StallGuardConfig config);

	class StallGuard final
	{
	public:
		explicit StallGuard(StallGuardConfig config = {});

		void Configure(StallGuardConfig config);
		const StallGuardConfig& Config() const { return m_config; }

		/// À appeler une fois par tour de boucle, \p nowSeconds = horloge monotone.
		/// \return vrai si ce tour suit un calage (écart ≥ stallSeconds depuis le tour
		///         précédent) ; la suspension démarre alors à \p nowSeconds.
		///         Le premier appel ne fait que poser la référence.
		bool OnLoop(double nowSeconds);

		/// Vrai si les contrôles de délai peuvent s'exécuter à \p nowSeconds (pas de
		/// calage récent, ou garde désactivée).
		bool ChecksAllowed(double nowSeconds) const;

		/// Nombre de calages détectés depuis le démarrage (télémétrie).
		uint64_t StallCount() const { return m_stallCount; }
		/// Durée du dernier calage détecté (s), 0 si aucun.
		double LastStallSeconds() const { return m_lastStallSeconds; }

	private:
		StallGuardConfig m_config{};
		bool m_hasPrevious = false;
		double m_previous = 0.0;
		double m_suspendedUntil = -1.0e18;
		uint64_t m_stallCount = 0;
		double m_lastStallSeconds = 0.0;
	};
}
