#pragma once
// ETAPE5.4 (chantier 5.5 du rapport City of Heroes) — métriques de jeu du shard.
//
// Le /metrics du shard n'exposait que les compteurs réseau TCP. On publie désormais, une
// fois par seconde, l'état de la simulation : cellules actives, entités, durée du tick,
// files d'attente (sauvegardes, Hello, lectures en base), mobs endormis, délestage,
// calages. Le thread de tick remplit un ShardGameplayMetrics et le dépose dans
// ShardMetricsBoard ; le thread HTTP le lit (copie sous verrou) et le formate.
//
// La même photo décide de la CONTRE-PRESSION remontée au master dans le heartbeat :
// un shard « saturé » (délestage actif ou file de sauvegarde débordée) ne reçoit plus de
// nouveaux joueurs tant que la situation dure (ShardRegistry le passe en Degraded).
//
// Formatage pur : testé seul (shard_metrics_tests).

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>

namespace engine::server
{
	/// Photo des métriques de jeu (publiée à 1 Hz par le thread de tick).
	struct ShardGameplayMetrics
	{
		uint64_t tick = 0;
		uint32_t tickHz = 0;
		double tickLastSeconds = 0.0;   ///< durée du dernier tick (réception + simulation + envois)
		double tickMaxSeconds = 0.0;    ///< pire tick depuis la photo précédente
		double tickLoad = 0.0;          ///< charge lissée (part du budget 1/tickHz)
		uint32_t clients = 0;           ///< joueurs connectés (UDP)
		uint32_t mobs = 0;
		uint32_t mobsAsleep = 0;
		uint32_t lootBags = 0;
		uint32_t regionsWarm = 0;       ///< cellules chaudes (actives ou encore chargées)
		uint32_t regionsTracked = 0;    ///< cellules suivies (pas encore déchargées)
		uint32_t pendingHellos = 0;     ///< Hello en attente d'admission
		uint32_t pendingCharacterLoads = 0; ///< lectures de personnage en cours (hors tick)
		uint32_t saveQueuePending = 0;
		bool saveQueueOverloaded = false; ///< file ≥ 75 % de sa capacité (CharacterSaveQueue::IsOverloaded)
		uint64_t saveQueueExecuted = 0;
		uint64_t saveQueueRejected = 0;
		uint64_t saveQueueCoalesced = 0;
		bool sheddingActive = false;
		uint64_t sheddingEnterCount = 0;
		uint64_t stallCount = 0;        ///< calages du tick (ETAPE5.2)
		uint64_t saveBlocksWritten = 0; ///< ETAPE5.5 : blocs réécrits en base
		uint64_t saveBlocksSkipped = 0; ///< ETAPE5.5 : blocs inchangés non réécrits
		uint64_t savesWithoutWrite = 0; ///< ETAPE5.5 : sauvegardes sans aucune écriture
	};

	/// Vrai si le shard doit refuser de nouveaux joueurs (contre-pression vers le master).
	inline bool IsSaturated(const ShardGameplayMetrics& m)
	{
		return m.sheddingActive || m.saveQueueOverloaded;
	}

	/// Charge lissée du tick en pourcentage du budget, bornée à [0 ; 1000] (heartbeat).
	uint16_t TickLoadPercent(const ShardGameplayMetrics& m);

	/// Au-delà de cet âge (s), la photo est périmée : le thread de tick ne publie plus
	/// (bloqué ou très lent) et le shard doit être considéré saturé.
	constexpr double kShardMetricsStaleSeconds = 3.0;

	/// Texte Prometheus (format d'exposition 0.0.4) des métriques `lcdlln_shard_*`.
	/// Ajouté à la sortie de BuildPrometheusText sur le /metrics du shard.
	/// \param ageSeconds âge de la photo (lcdlln_shard_metrics_age_seconds).
	std::string FormatShardMetricsPrometheus(const ShardGameplayMetrics& m, double ageSeconds = 0.0);

	/// Contre-pression à annoncer à partir d'une photo d'âge \p ageSeconds : saturé si la
	/// photo est périmée (tick bloqué) ou si IsSaturated.
	bool IsSaturatedOrStale(const ShardGameplayMetrics& m, double ageSeconds);

	/// Tableau d'affichage partagé entre le thread de tick (Publish) et les lecteurs
	/// (HTTP /metrics, heartbeat). Thread-safe.
	class ShardMetricsBoard final
	{
	public:
		/// \param nowSeconds horloge monotone du thread de tick (âge de la photo).
		void Publish(const ShardGameplayMetrics& m, double nowSeconds)
		{
			std::lock_guard lock(m_mutex);
			m_current = m;
			m_published = true;
			m_publishedAt = nowSeconds;
		}
		/// Âge de la dernière photo à \p nowSeconds (0 si aucune).
		double AgeSeconds(double nowSeconds) const
		{
			std::lock_guard lock(m_mutex);
			return m_published ? (std::max)(0.0, nowSeconds - m_publishedAt) : 0.0;
		}
		/// Dernière photo (valeurs nulles avant la première publication).
		ShardGameplayMetrics Snapshot() const
		{
			std::lock_guard lock(m_mutex);
			return m_current;
		}
		bool HasSnapshot() const
		{
			std::lock_guard lock(m_mutex);
			return m_published;
		}

	private:
		mutable std::mutex m_mutex;
		ShardGameplayMetrics m_current{};
		bool m_published = false;
		double m_publishedAt = 0.0;
	};
}
