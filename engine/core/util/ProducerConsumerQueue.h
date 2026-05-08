#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace engine::core::util
{
	/// File MPMC (multi-producteurs / multi-consommateurs) bornée
	/// optionnellement, avec attente bloquante via condition_variable.
	///
	/// Cas d'usage cibles : file de logs async, file de tasks DB
	/// (cf. CMANGOS.13 SqlDelayThread), bus de messages inter-threads.
	///
	/// Garanties :
	/// - Push/Pop sont thread-safe.
	/// - Cancel() débloque tous les WaitAndPop en cours et empêche les futurs
	///   de bloquer (utilisé pour le shutdown propre du worker).
	/// - L'ordre FIFO est respecté.
	///
	/// Implémentation : un seul mutex + une condition_variable. Pour les hot
	/// paths > 100k push/s, envisager une lock-free queue à la place
	/// (note dans le ticket CMANGOS.41).
	template <class T>
	class ProducerConsumerQueue
	{
	public:
		ProducerConsumerQueue() = default;

		ProducerConsumerQueue(const ProducerConsumerQueue&) = delete;
		ProducerConsumerQueue& operator=(const ProducerConsumerQueue&) = delete;
		ProducerConsumerQueue(ProducerConsumerQueue&&) = delete;
		ProducerConsumerQueue& operator=(ProducerConsumerQueue&&) = delete;

		/// Pousse une copie. Réveille un consommateur en attente (s'il y en a).
		void Push(const T& item)
		{
			{
				std::lock_guard<std::mutex> lk(m_mtx);
				m_q.push_back(item);
			}
			m_cv.notify_one();
		}

		/// Pousse en move. Réveille un consommateur en attente.
		void Push(T&& item)
		{
			{
				std::lock_guard<std::mutex> lk(m_mtx);
				m_q.push_back(std::move(item));
			}
			m_cv.notify_one();
		}

		/// Tente de retirer un item sans bloquer. Retourne false si la file
		/// est vide ou cancelled (et vide).
		bool TryPop(T& out)
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			if (m_q.empty())
			{
				return false;
			}
			out = std::move(m_q.front());
			m_q.pop_front();
			return true;
		}

		/// Bloque jusqu'à ce qu'un item arrive ou que \a timeout expire ou que
		/// Cancel() soit appelé. Retourne true si \a out a été rempli.
		///
		/// Note : si la file contient encore des items après Cancel(), ils sont
		/// drainés normalement (utile pour finir de flusher avant arrêt).
		bool WaitAndPop(T& out, std::chrono::milliseconds timeout = std::chrono::milliseconds::max())
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			const auto pred = [this] { return !m_q.empty() || m_cancelled; };
			if (timeout == std::chrono::milliseconds::max())
			{
				m_cv.wait(lk, pred);
			}
			else
			{
				if (!m_cv.wait_for(lk, timeout, pred))
				{
					return false;
				}
			}
			if (m_q.empty())
			{
				// Sortie via cancel sans item disponible.
				return false;
			}
			out = std::move(m_q.front());
			m_q.pop_front();
			return true;
		}

		/// Marque la file comme annulée. Tous les WaitAndPop en cours (et futurs)
		/// retournent false dès que la file est vidée.
		void Cancel()
		{
			{
				std::lock_guard<std::mutex> lk(m_mtx);
				m_cancelled = true;
			}
			m_cv.notify_all();
		}

		/// Retire le flag d'annulation. Utile pour les tests ; en prod, une
		/// queue cancelled est typiquement détruite.
		void Reset()
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_cancelled = false;
		}

		/// Taille instantanée. Indicative seulement (peut changer dès le retour).
		std::size_t Size() const
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			return m_q.size();
		}

		bool IsCancelled() const
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			return m_cancelled;
		}

	private:
		mutable std::mutex m_mtx;
		std::condition_variable m_cv;
		std::deque<T> m_q;
		bool m_cancelled = false;
	};
}
