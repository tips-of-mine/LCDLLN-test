#pragma once
// ETAPE5.3 (chantier 5.4 du rapport City of Heroes) — arrêt ordonné du cluster.
//
// Un arrêt (commande MJ `/shutdown <secondes> [motif]`, ou SIGTERM du master quand
// `master.shutdown.sigterm_delay_sec` > 0) se déroule en trois temps :
//   1. COMPTE À REBOURS : annonce aux joueurs (au départ puis aux paliers 10 min, 5 min,
//      2 min, 1 min, 30 s, 15 s, 10 s, 5…1 s) ; les NOUVELLES entrées en jeu sont
//      refusées dès le départ (les joueurs déjà en jeu continuent) ;
//   2. VIDAGE : à zéro, chaque shard reçoit l'ordre de sauvegarder tous ses joueurs, de
//      vider sa file SQL et de s'arrêter proprement (code de sortie 0) ; les clients en
//      jeu sont renvoyés à l'écran de connexion ;
//   3. FIN : après `drainSeconds`, le master s'arrête à son tour.
// Le compte à rebours peut être annulé (`/shutdown cancel`) ; le vidage, non.
//
// Module sans E/S (horloge passée en argument, en secondes) : testé seul
// (cluster_shutdown_tests). Thread-safe : la commande MJ arrive sur un thread de
// traitement, Tick tourne dans la boucle principale du master.

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace engine::server
{
	enum class ClusterShutdownPhase : uint8_t
	{
		Idle = 0,      ///< aucun arrêt en cours
		Countdown = 1, ///< annonces, nouvelles entrées refusées
		Draining = 2,  ///< shards en cours d'arrêt
		Done = 3,      ///< le master doit s'arrêter
	};

	/// Action que la boucle principale doit exécuter (renvoyée par ClusterShutdown::Tick).
	struct ClusterShutdownAction
	{
		enum class Kind : uint8_t
		{
			Announce = 0,  ///< prévenir les joueurs : arrêt dans remainingSeconds
			StopShards = 1,///< ordonner aux shards de sauvegarder et de s'arrêter
			Exit = 2,      ///< arrêter le master
		};
		Kind kind = Kind::Announce;
		uint32_t remainingSeconds = 0; ///< Announce : secondes restantes (arrondies au-dessus)
	};

	class ClusterShutdown final
	{
	public:
		/// Délai maximal accepté pour un compte à rebours (24 h).
		static constexpr uint32_t kMaxDelaySeconds = 24u * 3600u;

		/// \param drainSeconds durée du vidage avant l'arrêt du master (bornée à [1 ; 600] s).
		explicit ClusterShutdown(double drainSeconds = 15.0);

		/// Lance un arrêt dans \p delaySeconds (borné à kMaxDelaySeconds ; 0 = immédiat)
		/// avec le motif \p reason. Refusé (false) si un arrêt est déjà en vidage ; un
		/// nouvel appel pendant le compte à rebours le REPROGRAMME (nouveau délai, nouveau
		/// motif) et réannonce. \p nowSeconds = horloge monotone.
		bool Start(double nowSeconds, uint32_t delaySeconds, std::string reason);

		/// Annule le compte à rebours. \return false s'il n'y en a pas (ou vidage commencé).
		bool Cancel();

		/// Fait avancer l'arrêt ; renvoie les actions à exécuter, dans l'ordre.
		std::vector<ClusterShutdownAction> Tick(double nowSeconds);

		ClusterShutdownPhase Phase() const;
		/// Vrai dès le début du compte à rebours : refuser les nouvelles entrées en jeu.
		bool AdmissionsBlocked() const;
		std::string Reason() const;
		/// Secondes restantes avant le vidage (0 hors compte à rebours).
		uint32_t RemainingSeconds(double nowSeconds) const;

	private:
		mutable std::mutex m_mutex;
		double m_drainSeconds = 15.0;
		ClusterShutdownPhase m_phase = ClusterShutdownPhase::Idle;
		double m_deadline = 0.0;       ///< fin du compte à rebours
		double m_exitAt = 0.0;         ///< fin du vidage
		std::string m_reason;
		bool m_announceStart = false;  ///< annonce de départ (ou de reprogrammation) à émettre
		uint32_t m_lastAnnouncedStep = 0; ///< dernier palier annoncé (0 = aucun)
	};

	/// Texte d'annonce en français pour \p remainingSeconds (« Arrêt du serveur dans 5 min :
	/// motif »). Exposé pour les tests et pour la réponse de la commande MJ.
	std::string FormatShutdownAnnouncement(uint32_t remainingSeconds, const std::string& reason);
}
