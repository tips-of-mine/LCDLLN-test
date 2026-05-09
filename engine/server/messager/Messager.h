#pragma once
// CMANGOS.35 (Phase 4.35a) — Messager : queue MPSC simple (multi producer
// single consumer) pour passer des messages entre threads sans verrou par
// message dans le hot path. Header-only, std::mutex (pas de lock-free).
//
// Usage typique : workers async (DB, AI tick, scripts) postent des messages,
// le main game loop draine la queue 1x par tick.

#include <cstdint>
#include <mutex>
#include <vector>
#include <utility>

namespace engine::server::messager
{
	template<typename T>
	class Messager
	{
	public:
		void Post(T msg)
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_queue.push_back(std::move(msg));
		}

		/// Vide la queue dans \p out. Retourne le nombre de messages drainees.
		/// \p out est append-only (les anciens elements sont conserves).
		size_t Drain(std::vector<T>& out)
		{
			std::vector<T> tmp;
			{
				std::lock_guard<std::mutex> lk(m_mtx);
				tmp.swap(m_queue);
			}
			out.insert(out.end(),
			           std::make_move_iterator(tmp.begin()),
			           std::make_move_iterator(tmp.end()));
			return tmp.size();
		}

		size_t Size() const
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			return m_queue.size();
		}

	private:
		mutable std::mutex m_mtx;
		std::vector<T>     m_queue;
	};
}
