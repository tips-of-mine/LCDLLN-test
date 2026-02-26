#pragma once

/**
 * @file JobSystem.h
 * @brief Thread-pool based job system with work-stealing, JobHandle and JobGroup.
 *
 * Ticket: M00.3
 * Namespace: engine::core::jobs
 *
 * API summary:
 *   Jobs::Init(nWorkers)               — start thread pool (0 = hardware_concurrency - 1)
 *   Jobs::Enqueue(fn) -> JobHandle     — submit a single job; returns a handle to wait on
 *   Jobs::EnqueueGroup(fns) -> JobGroup— submit multiple jobs as a group
 *   Jobs::Wait(JobHandle)              — block until the job is done (pumps jobs to avoid deadlock)
 *   Jobs::Wait(JobGroup)               — block until all jobs in the group are done
 *   Jobs::Shutdown()                   — stop all worker threads (joins them)
 *
 * Design notes (MVP):
 *   - One shared mutex-protected FIFO queue (MPSC-safe via mutex).
 *   - Workers sleep on a condition variable; woken when work is available.
 *   - JobHandle wraps a shared atomic counter (1 = pending, 0 = done).
 *   - JobGroup aggregates multiple handles via a single shared counter.
 *   - Wait() can be called from a worker thread: it pumps the queue instead
 *     of blocking unconditionally, avoiding deadlock on nested jobs.
 */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace engine::core::jobs {

// ---------------------------------------------------------------------------
// JobHandle
// ---------------------------------------------------------------------------

/**
 * @brief Lightweight handle to a single submitted job.
 *
 * Internally holds a shared atomic counter that is decremented to 0 when the
 * job finishes.  Copying a handle is cheap (shared_ptr copy).
 */
class JobHandle {
public:
    /// Default-constructed handle is already "done" (nullptr counter).
    JobHandle() = default;

    /// @return true if the job has completed (or handle is empty).
    [[nodiscard]] bool IsDone() const noexcept;

private:
    friend class JobSystem;
    friend class JobGroup;

    /// Created by JobSystem::Enqueue; counter starts at 1, decremented on completion.
    explicit JobHandle(std::shared_ptr<std::atomic<int>> counter)
        : m_counter(std::move(counter)) {}

    std::shared_ptr<std::atomic<int>> m_counter;
};

// ---------------------------------------------------------------------------
// JobGroup
// ---------------------------------------------------------------------------

/**
 * @brief Groups multiple jobs so they can be waited on collectively.
 *
 * A single shared atomic counter tracks how many jobs in the group remain.
 * All jobs in the group share the same counter; it is decremented once per
 * completed job.
 */
class JobGroup {
public:
    /// Default-constructed group has no pending jobs.
    JobGroup() = default;

    /// @return true when all jobs in the group have completed.
    [[nodiscard]] bool IsDone() const noexcept;

    /// @return number of jobs still pending.
    [[nodiscard]] int  PendingCount() const noexcept;

private:
    friend class JobSystem;

    explicit JobGroup(std::shared_ptr<std::atomic<int>> counter)
        : m_counter(std::move(counter)) {}

    std::shared_ptr<std::atomic<int>> m_counter;
};

// ---------------------------------------------------------------------------
// JobSystem
// ---------------------------------------------------------------------------

/**
 * @brief Singleton-style job system managing a worker-thread pool and a
 *        shared work queue.
 *
 * Only one instance should be active at a time.  All public methods are
 * thread-safe unless noted otherwise.
 */
class JobSystem {
public:
    JobSystem()  = default;
    ~JobSystem() = default;

    // Non-copyable / non-movable.
    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&)                 = delete;
    JobSystem& operator=(JobSystem&&)      = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /**
     * @brief Initialises the job system and spawns worker threads.
     *
     * @param nWorkers  Number of worker threads.
     *                  0 (default) → std::thread::hardware_concurrency() - 1,
     *                  clamped to at least 1.
     */
    void Init(std::size_t nWorkers = 0);

    /**
     * @brief Stops all worker threads and joins them.
     *
     * Remaining jobs in the queue are discarded.  Must be called before
     * destruction if Init() was called.
     */
    void Shutdown();

    // -----------------------------------------------------------------------
    // Job submission
    // -----------------------------------------------------------------------

    /**
     * @brief Submits a single job to the queue.
     *
     * @param fn  Callable with signature `void()`.
     * @return    A JobHandle that becomes "done" once @p fn returns.
     */
    [[nodiscard]] JobHandle Enqueue(std::function<void()> fn);

    /**
     * @brief Submits a batch of jobs that share a common completion counter.
     *
     * @param fns  Vector of callables; each has signature `void()`.
     * @return     A JobGroup that becomes "done" when all jobs have finished.
     */
    [[nodiscard]] JobGroup EnqueueGroup(std::vector<std::function<void()>> fns);

    // -----------------------------------------------------------------------
    // Waiting
    // -----------------------------------------------------------------------

    /**
     * @brief Blocks the calling thread until the job referenced by @p handle
     *        has completed.
     *
     * If called from a worker thread, the caller also processes queued jobs
     * to prevent deadlock on nested job submissions.
     */
    void Wait(const JobHandle& handle);

    /**
     * @brief Blocks the calling thread until all jobs in @p group have
     *        completed.
     *
     * Same deadlock-avoidance behaviour as Wait(JobHandle).
     */
    void Wait(const JobGroup& group);

    // -----------------------------------------------------------------------
    // Global accessor (convenience singleton)
    // -----------------------------------------------------------------------

    /// Returns the process-wide JobSystem instance.
    static JobSystem& Get() noexcept;

private:
    // -----------------------------------------------------------------------
    // Internal types
    // -----------------------------------------------------------------------

    /// A job entry stored in the queue.
    struct Job {
        std::function<void()>              fn;      ///< Work to execute.
        std::shared_ptr<std::atomic<int>>  counter; ///< Completion counter to decrement.
    };

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Try to pop one job from the queue (non-blocking).  Returns false if empty.
    bool TryPop(Job& out);

    /// Worker thread main loop.
    void WorkerLoop(std::size_t workerIndex);

    /// Executes one job: calls fn(), decrements its counter, notifies waiters.
    void ExecuteJob(Job& job);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    std::vector<std::thread>   m_workers;   ///< Worker thread pool.
    std::vector<Job>           m_queue;     ///< Shared FIFO work queue.

    mutable std::mutex              m_queueMutex; ///< Guards m_queue.
    std::condition_variable         m_workerCV;   ///< Workers wait here.
    std::condition_variable         m_doneCV;     ///< Waiters sleep here.

    std::atomic<bool>          m_stop{ false }; ///< Set to true by Shutdown().

    /// Thread-local flag: true for threads owned by this JobSystem.
    /// Used by Wait() to pump jobs instead of sleeping unconditionally.
    static thread_local bool   s_isWorker;
};

// ---------------------------------------------------------------------------
// Global API helpers (thin wrappers around JobSystem::Get())
// ---------------------------------------------------------------------------

namespace Jobs {

/**
 * @brief Initialises the global job system.
 * @param nWorkers  0 = hardware_concurrency - 1 (clamped to 1).
 */
inline void Init(std::size_t nWorkers = 0) {
    JobSystem::Get().Init(nWorkers);
}

/** @brief Submits a single job and returns its handle. */
[[nodiscard]] inline JobHandle Enqueue(std::function<void()> fn) {
    return JobSystem::Get().Enqueue(std::move(fn));
}

/** @brief Submits a batch of jobs and returns a group handle. */
[[nodiscard]] inline JobGroup EnqueueGroup(std::vector<std::function<void()>> fns) {
    return JobSystem::Get().EnqueueGroup(std::move(fns));
}

/** @brief Waits until a single job handle is done. */
inline void Wait(const JobHandle& h) { JobSystem::Get().Wait(h); }

/** @brief Waits until all jobs in a group are done. */
inline void Wait(const JobGroup& g)  { JobSystem::Get().Wait(g); }

/** @brief Shuts down the global job system and joins all workers. */
inline void Shutdown() { JobSystem::Get().Shutdown(); }

} // namespace Jobs

} // namespace engine::core::jobs
