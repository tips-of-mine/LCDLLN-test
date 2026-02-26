#pragma once
// engine/core/jobs/JobSystem.h
// M00.3 — Job System: thread pool, work-stealing queues, job groups.
//
// API overview:
//   Jobs::Init(n)              — start n worker threads (default: hardware_concurrency - 1)
//   Jobs::Shutdown()           — signal stop + join all workers
//   Jobs::Enqueue(fn) -> JobHandle — submit a single job, returns a waitable handle
//   JobGroup                   — batch of jobs with a shared completion counter
//   group.Add(fn)              — enqueue a job into the group
//   group.Wait()               — block (pumping jobs) until all group jobs complete
//   Jobs::Wait(handle)         — block until a single job handle completes
//
// Thread safety: all public functions are thread-safe.
// MVP implementation: mutex + condvar queue (lock-free later).

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <deque>

namespace engine::core::jobs {

// ---------------------------------------------------------------------------
// JobHandle — tracks completion of a single job
// ---------------------------------------------------------------------------

/// Shared state for a single job's completion.
/// Internally reference-counted so JobHandle is copyable.
struct JobHandle {
    /// Returns true when the associated job has finished executing.
    [[nodiscard]] bool IsDone() const noexcept;

    /// Internal: shared atomic counter (0 = done, 1 = pending).
    /// Public so JobGroup can access without friending.
    std::shared_ptr<std::atomic<int>> _counter;
};

// ---------------------------------------------------------------------------
// JobGroup — batch of logically related jobs
// ---------------------------------------------------------------------------

/// Groups multiple jobs under a single wait point.
/// Usage:
///   JobGroup g;
///   g.Add([]{ doWork(); });
///   g.Add([]{ doWork(); });
///   g.Wait(); // blocks until all jobs done
class JobGroup {
public:
    JobGroup() = default;
    ~JobGroup() = default;

    /// Not copyable; move is fine.
    JobGroup(const JobGroup&)            = delete;
    JobGroup& operator=(const JobGroup&) = delete;
    JobGroup(JobGroup&&)                 = default;
    JobGroup& operator=(JobGroup&&)      = default;

    /// Enqueue one job into this group.
    /// Safe to call from multiple threads simultaneously (MPSC).
    void Add(std::function<void()> fn);

    /// Block until ALL jobs added to this group have finished.
    /// Pumps the global job queue while waiting to avoid deadlock when called
    /// from a worker thread.
    void Wait();

    /// Returns true when all jobs have completed (non-blocking).
    [[nodiscard]] bool IsDone() const noexcept;

private:
    std::atomic<int> _pending{0};
    // Condition used by the Wait path when called from non-worker threads.
    mutable std::mutex              _mtx;
    mutable std::condition_variable _cv;
};

// ---------------------------------------------------------------------------
// Jobs namespace — global API
// ---------------------------------------------------------------------------

namespace Jobs {

/// Initialise the job system with @p numWorkers worker threads.
/// Pass 0 to use (hardware_concurrency - 1), minimum 1.
/// Must be called exactly once before any Enqueue/Wait.
void Init(unsigned int numWorkers = 0);

/// Gracefully shut down: signal workers to stop, drain remaining jobs, join.
void Shutdown();

/// Submit a single job to the global queue.
/// Returns a JobHandle that can be passed to Wait().
JobHandle Enqueue(std::function<void()> fn);

/// Block until the job referenced by @p handle has completed.
/// Pumps other jobs while waiting to avoid deadlock from worker threads.
void Wait(JobHandle handle);

/// Returns the number of worker threads currently running.
[[nodiscard]] unsigned int WorkerCount() noexcept;

} // namespace Jobs

} // namespace engine::core::jobs
