#include "engine/core/jobs/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace engine::core::jobs
{
	namespace
	{
		thread_local int g_workerIndex = -1;

		struct Job
		{
			JobFn fn;
			JobGroup* group = nullptr;
		};

		class BoundedDeque final
		{
		public:
			explicit BoundedDeque(size_t capacity)
				: m_capacity(std::max<size_t>(1, capacity))
				, m_buffer(m_capacity)
			{
			}

			BoundedDeque(const BoundedDeque&) = delete;
			BoundedDeque& operator=(const BoundedDeque&) = delete;

			bool empty() const { return m_size == 0; }
			bool full() const { return m_size == m_capacity; }

			bool push_back(Job&& j)
			{
				if (full())
				{
					return false;
				}
				m_buffer[m_tail] = std::move(j);
				m_tail = (m_tail + 1) % m_capacity;
				++m_size;
				return true;
			}

			bool pop_front(Job& out)
			{
				if (empty())
				{
					return false;
				}
				out = std::move(m_buffer[m_head]);
				m_head = (m_head + 1) % m_capacity;
				--m_size;
				return true;
			}

			bool pop_back(Job& out)
			{
				if (empty())
				{
					return false;
				}
				m_tail = (m_tail + m_capacity - 1) % m_capacity;
				out = std::move(m_buffer[m_tail]);
				--m_size;
				return true;
			}

		private:
			size_t m_capacity = 1;
			std::vector<Job> m_buffer;
			size_t m_head = 0;
			size_t m_tail = 0;
			size_t m_size = 0;
		};

		struct Worker
		{
			std::mutex mutex;
			BoundedDeque local;
			std::jthread thread;

			explicit Worker(size_t localCapacity)
				: local(localCapacity)
			{
			}
		};

		struct State
		{
			std::atomic<bool> running{ false };
			std::atomic<bool> stop{ false };

			std::mutex globalMutex;
			std::condition_variable globalCv;
			BoundedDeque globalQueue{ 262144 };

			std::vector<std::unique_ptr<Worker>> workers;
		};

		State& S()
		{
			static State s;
			return s;
		}

		void FinishJob(Job& job)
		{
			if (job.group)
			{
				job.group->m_count.fetch_sub(1, std::memory_order_release);
			}
			S().globalCv.notify_all();
		}

		void Execute(Job& job)
		{
			if (job.fn.Valid())
			{
				job.fn.Invoke();
			}
			FinishJob(job);
		}

		bool TryPopLocal(int idx, Job& out)
		{
			auto& st = S();
			if (idx < 0 || idx >= static_cast<int>(st.workers.size()))
			{
				return false;
			}
			auto& w = *st.workers[static_cast<size_t>(idx)];
			std::scoped_lock lock(w.mutex);
			return w.local.pop_back(out);
		}

		bool TrySteal(int thiefIdx, Job& out)
		{
			auto& st = S();
			const size_t n = st.workers.size();
			if (n == 0)
			{
				return false;
			}

			// Simple work-stealing: iterate over other workers and steal from the front.
			const size_t start = (thiefIdx < 0) ? 0 : (static_cast<size_t>(thiefIdx) + 1) % n;
			for (size_t i = 0; i < n; ++i)
			{
				const size_t victim = (start + i) % n;
				if (static_cast<int>(victim) == thiefIdx)
				{
					continue;
				}
				auto& w = *st.workers[victim];
				std::scoped_lock lock(w.mutex);
				if (w.local.pop_front(out))
				{
					return true;
				}
			}
			return false;
		}

		bool TryPopGlobal(Job& out)
		{
			auto& st = S();
			std::scoped_lock lock(st.globalMutex);
			return st.globalQueue.pop_front(out);
		}

		bool TryPopGlobalBlocking(Job& out)
		{
			auto& st = S();
			std::unique_lock lock(st.globalMutex);
			st.globalCv.wait(lock, [&]()
			{
				return st.stop.load(std::memory_order_relaxed) || !st.globalQueue.empty();
			});
			if (st.stop.load(std::memory_order_relaxed))
			{
				return false;
			}
			return st.globalQueue.pop_front(out);
		}

		bool TryExecuteOne()
		{
			Job job;

			if (TryPopLocal(g_workerIndex, job))
			{
				Execute(job);
				return true;
			}
			if (TrySteal(g_workerIndex, job))
			{
				Execute(job);
				return true;
			}
			if (TryPopGlobal(job))
			{
				Execute(job);
				return true;
			}
			return false;
		}

		void WorkerLoop(int index)
		{
			g_workerIndex = index;
			while (!S().stop.load(std::memory_order_relaxed))
			{
				if (TryExecuteOne())
				{
					continue;
				}

				Job job;
				if (TryPopGlobalBlocking(job))
				{
					Execute(job);
				}
			}
			g_workerIndex = -1;
		}
	}

	void Jobs::Init(uint32_t workerCount)
	{
		auto& st = S();
		if (st.running.exchange(true, std::memory_order_acq_rel))
		{
			return;
		}

		st.stop.store(false, std::memory_order_relaxed);

		uint32_t hc = std::thread::hardware_concurrency();
		if (hc == 0)
		{
			hc = 4;
		}
		if (workerCount == 0)
		{
			workerCount = (hc > 1) ? (hc - 1) : 1;
		}

		st.workers.clear();
		st.workers.reserve(workerCount);
		for (uint32_t i = 0; i < workerCount; ++i)
		{
			st.workers.emplace_back(std::make_unique<Worker>(65536));
		}

		for (uint32_t i = 0; i < workerCount; ++i)
		{
			st.workers[i]->thread = std::jthread([i]()
			{
				WorkerLoop(static_cast<int>(i));
			});
		}

		LOG_INFO(Jobs, "Jobs::Init workers={}", workerCount);
	}

	void Jobs::Shutdown()
	{
		auto& st = S();
		if (!st.running.exchange(false, std::memory_order_acq_rel))
		{
			return;
		}

		st.stop.store(true, std::memory_order_relaxed);
		st.globalCv.notify_all();

		// jthread joins on destruction.
		st.workers.clear();

		LOG_INFO(Jobs, "Jobs::Shutdown complete");
	}

	void Jobs::EnqueueInternal(JobGroup* group, JobFn&& fn)
	{
		auto& st = S();
		if (!st.running.load(std::memory_order_acquire))
		{
			LOG_FATAL(Jobs, "Jobs::Enqueue called before Jobs::Init");
		}

		Job job;
		job.fn = std::move(fn);
		job.group = group;

		// Prefer local queue when enqueuing from a worker; otherwise use global MPSC.
		if (g_workerIndex >= 0 && g_workerIndex < static_cast<int>(st.workers.size()))
		{
			auto& w = *st.workers[static_cast<size_t>(g_workerIndex)];
			{
				std::scoped_lock lock(w.mutex);
				if (w.local.push_back(std::move(job)))
				{
					st.globalCv.notify_one();
					return;
				}
			}
		}

		{
			std::scoped_lock lock(st.globalMutex);
			if (!st.globalQueue.push_back(std::move(job)))
			{
				LOG_FATAL(Jobs, "Global job queue overflow (increase capacity)");
			}
		}
		st.globalCv.notify_one();
	}

	void Jobs::Wait(JobHandle& handle)
	{
		Wait(handle.m_group);
	}

	void Jobs::Wait(JobGroup& group)
	{
		auto& st = S();
		while (!group.IsDone())
		{
			if (TryExecuteOne())
			{
				continue;
			}

			// No local work found: wait briefly for new jobs/completions.
			std::unique_lock lock(st.globalMutex);
			st.globalCv.wait_for(lock, std::chrono::milliseconds(1));
		}
	}
}

