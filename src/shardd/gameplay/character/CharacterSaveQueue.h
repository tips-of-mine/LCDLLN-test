#pragma once
// ETAPE2.2 (chantier 3.2) — CharacterSaveQueue : sauvegardes de personnages hors
// du tick de jeu.
//
// Le tick prépare un instantané de l'état (copie) et le dépose dans la file ;
// quelques threads de travail l'écrivent en base. Règles :
//   - un personnage est toujours traité par le même thread (id modulo nombre de
//     threads) : ses écritures gardent leur ordre ;
//   - une sauvegarde pas encore commencée est REMPLACÉE par la suivante du même
//     personnage (seul l'état le plus récent compte) ;
//   - file pleine : l'appelant reçoit Rejected et exécute la sauvegarde lui-même
//     (rien n'est perdu, le tick ralentit au lieu de jeter des données) ;
//   - Flush(id) attend la fin des travaux d'un personnage (relecture juste
//     après écriture, transactions à deux personnages) ;
//   - Stop() exécute tout ce qui reste avant de rendre la main.
//
// Aucune dépendance MySQL : le travail est une fonction fournie par l'appelant
// (testable sans base, cf. character_save_queue_tests). Les crochets
// onThreadStart/onThreadEnd servent à mysql_thread_init/mysql_thread_end.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace engine::server
{
	class CharacterSaveQueue
	{
	public:
		/// Travail à exécuter sur un thread de la file.
		using Job = std::function<void()>;
		/// Crochet appelé au démarrage / à l'arrêt de chaque thread.
		using ThreadHook = std::function<void()>;

		/// Résultat d'un dépôt.
		enum class EnqueueResult : uint8_t
		{
			Queued,    ///< Ajouté à la file.
			Coalesced, ///< A remplacé une sauvegarde en attente du même personnage.
			Rejected,  ///< File pleine ou arrêtée : l'appelant doit exécuter le travail lui-même.
		};

		/// Compteurs cumulés (diagnostic, logs).
		struct Stats
		{
			uint64_t queued = 0;
			uint64_t coalesced = 0;
			uint64_t rejected = 0;
			uint64_t executed = 0;
		};

		/// \param workerCount nombre de threads (borné à 1..16).
		/// \param maxPending nombre maximal de travaux en attente, tous threads confondus.
		CharacterSaveQueue(size_t workerCount, size_t maxPending,
			ThreadHook onThreadStart = {}, ThreadHook onThreadEnd = {});
		/// Appelle Stop() si besoin.
		~CharacterSaveQueue();
		CharacterSaveQueue(const CharacterSaveQueue&) = delete;
		CharacterSaveQueue& operator=(const CharacterSaveQueue&) = delete;

		/// Démarre les threads. Sans effet si déjà démarrée.
		void Start();
		/// Refuse les nouveaux dépôts, exécute ceux en attente, puis arrête les threads.
		void Stop();
		/// Vrai entre Start() et Stop().
		bool IsRunning() const;

		/// Dépose la sauvegarde complète d'un personnage. Remplace la sauvegarde du
		/// même personnage encore en attente si c'est son dernier travail en file.
		EnqueueResult EnqueueSave(uint64_t characterId, Job job);
		/// Dépose un travail jamais fusionné (exécuté dans l'ordre de dépôt).
		EnqueueResult EnqueueOrdered(uint64_t characterId, Job job);

		/// Attend que tous les travaux de \p characterId déposés avant l'appel soient
		/// terminés, au plus \p timeout. Retour immédiat si la file est arrêtée ou si
		/// rien n'est en attente. Ne pas appeler depuis un thread de la file.
		/// \return false si le délai est dépassé (travaux encore en cours).
		bool Flush(uint64_t characterId, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));
		/// Attend que la file soit vide.
		void FlushAll();

		/// Travaux en attente (instantané).
		size_t PendingCount() const;
		/// Vrai quand la file dépasse 75 % de sa capacité.
		bool IsOverloaded() const;
		/// Copie des compteurs.
		Stats GetStats() const;
		/// Nombre de threads.
		size_t WorkerCount() const { return m_workers.size(); }

		/// Thread qui traite \p characterId (id modulo nombre de threads).
		static size_t WorkerIndexFor(uint64_t characterId, size_t workerCount) noexcept
		{
			return workerCount == 0u ? 0u : static_cast<size_t>(characterId % workerCount);
		}

	private:
		struct Entry
		{
			uint64_t characterId = 0;
			Job job;
			bool isSave = false;
		};

		struct Worker
		{
			std::mutex mutex;
			std::condition_variable wake;  ///< nouveau travail ou arrêt
			std::condition_variable done;  ///< un travail vient de finir
			std::deque<Entry> queue;
			bool accepting = false; ///< vrai entre Start() et Stop()
			bool stopping = false;  ///< Stop() demandé : vider la file puis sortir
			bool busy = false;
			uint64_t busyCharacterId = 0;
			std::thread thread;
		};

		EnqueueResult Enqueue(uint64_t characterId, Job job, bool isSave);
		void WorkerLoop(Worker& worker);

		std::vector<std::unique_ptr<Worker>> m_workers;
		const size_t m_maxPending;
		ThreadHook m_onThreadStart;
		ThreadHook m_onThreadEnd;

		std::atomic<bool> m_running{false};
		std::atomic<size_t> m_pending{0};
		std::atomic<uint64_t> m_queued{0};
		std::atomic<uint64_t> m_coalesced{0};
		std::atomic<uint64_t> m_rejected{0};
		std::atomic<uint64_t> m_executed{0};
	};
}
