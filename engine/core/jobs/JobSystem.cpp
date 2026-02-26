/**
 * @file JobSystem.cpp
 * @brief Implementation of the thread-pool job system (M00.3).
 *
 * Design decisions:
 *   - Single shared mutex + condition variable (MVP; lock-free optimisation
 *     is deferred to a later ticket).
 *   - Worker threads park on m_workerCV when the queue is empty.
 *   - Wait() callers park on m_doneCV; if the caller is itself a worker it
 *     pumps jobs to prevent deadlock.
 *   - Each job carries a shared_ptr<atomic<int>> counter.  The counter
 *     starts at 1 (single job) or N (group of N jobs) and is decremented
 *     to 0 when all associated jobs finish.  m_doneCV is notified on every
 *     decrement so that Wait() callers can recheck.
 */

#include "JobSystem.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace engine::core::jobs {

// ---------------------------------------------------------------------------
// Thread-local: set to true for threads owned by JobSystem
// ---------------------------------------------------------------------------
thread_local bool JobSystem::s_isWorker = false;

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

/*static*/
JobSystem& JobSystem::Get() noexcept {
    static JobSystem s_instance;
    return s_instance;
}

// ---------------------------------------------------------------------------
// JobHandle
// ---------------------------------------------------------------------------

bool JobHandle::IsDone() const noexcept {
    if (!m_counter) { return true; }
    return m_counter->load(std::memory_order_acquire) == 0;
}

// ---------------------------------------------------------------------------
// JobGroup
// ---------------------------------------------------------------------------

bool JobGroup::IsDone() const noexcept {
    if (!m_counter) { return true; }
    return m_counter->load(std::memory_order_acquire) == 0;
}

int JobGroup::PendingCount() const noexcept {
    if (!m_counter) { return 0; }
    return m_counter->load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// JobSystem — lifecycle
// ---------------------------------------------------------------------------

void JobSystem::Init(std::size_t nWorkers) {
    if (!m_workers.empty()) {
        throw std::logic_error("JobSystem::Init called more than once without Shutdown");
    }

    if (nWorkers == 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        nWorkers = (hw > 1u) ? static_cast<std::size_t>(hw - 1u) : 1u;
    }

    m_stop.store(false, std::memory_order_relaxed);
    m_workers.reserve(nWorkers);

    for (std::size_t i = 0; i < nWorkers; ++i) {
        m_workers.emplace_back([this, i] { WorkerLoop(i); });
    }
}

void JobSystem::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stop.store(true, std::memory_order_release);
    }
    m_workerCV.notify_all();

    for (auto& t : m_workers) {
        if (t.joinable()) { t.join(); }
    }
    m_workers.clear();

    // Clear any remaining jobs (discarded on shutdown).
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.clear();
    }
}

// ---------------------------------------------------------------------------
// JobSystem — job submission
// ---------------------------------------------------------------------------

JobHandle JobSystem::Enqueue(std::function<void()> fn) {
    auto counter = std::make_shared<std::atomic<int>>(1);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back(Job{ std::move(fn), counter });
    }
    m_workerCV.notify_one();

    return JobHandle{ std::move(counter) };
}

JobGroup JobSystem::EnqueueGroup(std::vector<std::function<void()>> fns) {
    if (fns.empty()) {
        return JobGroup{};
    }

    const int count   = static_cast<int>(fns.size());
    auto sharedCounter = std::make_shared<std::atomic<int>>(count);

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (auto& fn : fns) {
            m_queue.push_back(Job{ std::move(fn), sharedCounter });
        }
    }
    m_workerCV.notify_all();

    return JobGroup{ std::move(sharedCounter) };
}

// ---------------------------------------------------------------------------
// JobSystem — waiting
// ---------------------------------------------------------------------------

void JobSystem::Wait(const JobHandle& handle) {
    if (!handle.m_counter) { return; }

    if (s_isWorker) {
        // Called from a worker: pump jobs to avoid deadlock.
        while (!handle.IsDone()) {
            Job job;
            if (TryPop(job)) {
                ExecuteJob(job);
            } else {
                // Yield briefly to avoid burning CPU while genuinely empty.
                std::this_thread::yield();
            }
        }
    } else {
        // Called from a non-worker: sleep until done, but also pump to help.
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_doneCV.wait(lock, [&] {
            return handle.IsDone();
        });
    }
}

void JobSystem::Wait(const JobGroup& group) {
    if (!group.m_counter) { return; }

    if (s_isWorker) {
        while (!group.IsDone()) {
            Job job;
            if (TryPop(job)) {
                ExecuteJob(job);
            } else {
                std::this_thread::yield();
            }
        }
    } else {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_doneCV.wait(lock, [&] {
            return group.IsDone();
        });
    }
}

// ---------------------------------------------------------------------------
// JobSystem — internal helpers
// ---------------------------------------------------------------------------

bool JobSystem::TryPop(Job& out) {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    if (m_queue.empty()) { return false; }
    out = std::move(m_queue.front());
    m_queue.erase(m_queue.begin());
    return true;
}

void JobSystem::ExecuteJob(Job& job) {
    // Execute the user function.
    job.fn();

    // Decrement counter.  If it reaches 0, notify all waiters.
    if (job.counter) {
        const int prev = job.counter->fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1) {
            // Last job in this handle/group — wake waiting threads.
            m_doneCV.notify_all();
        }
    }
}

void JobSystem::WorkerLoop(std::size_t /*workerIndex*/) {
    s_isWorker = true;

    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            // Sleep until there is work or we're asked to stop.
            m_workerCV.wait(lock, [this] {
                return m_stop.load(std::memory_order_acquire) || !m_queue.empty();
            });

            if (m_stop.load(std::memory_order_acquire) && m_queue.empty()) {
                return; // Clean exit.
            }

            if (!m_queue.empty()) {
                job = std::move(m_queue.front());
                m_queue.erase(m_queue.begin());
            } else {
                continue; // Spurious wakeup or stop without work.
            }
        }

        ExecuteJob(job);
    }
}

} // namespace engine::core::jobs
