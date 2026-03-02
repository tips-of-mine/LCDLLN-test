/**
 * @file JobSystem.h
 * @brief Thread pool, thread-safe queue, JobHandle/JobGroup with Wait/IsDone. Wait pumps jobs to avoid deadlock when called from a worker.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace engine::core {

/** Opaque handle for a single enqueued job; use with Wait/IsDone. */
class JobHandle {
public:
    JobHandle() = default;
    bool Valid() const { return m_counterIndex != kInvalid; }
    bool operator==(const JobHandle& o) const { return m_counterIndex == o.m_counterIndex; }

private:
    friend class Jobs;
    static constexpr uint32_t kInvalid = 0xFFFFFFFFu;
    explicit JobHandle(uint32_t counterIndex) : m_counterIndex(counterIndex) {}
    uint32_t m_counterIndex = kInvalid;
};

/** Group of jobs; enqueue with EnqueueToGroup then Wait(group) until all are done. */
class JobGroup {
public:
    JobGroup() = default;
    /** True when all jobs in the group have completed. */
    bool IsDone() const { return m_pending.load(std::memory_order_acquire) == 0; }

private:
    friend class Jobs;
    std::atomic<uint32_t> m_pending{0};
};

/**
 * Job system: Init(n), Enqueue(fn), EnqueueToGroup(group, fn), Wait(handle|group), IsDone.
 * Wait also runs pending jobs on the calling thread to avoid deadlock when Wait is called from a worker.
 */
class Jobs {
public:
    /** Initialize thread pool (n = number of worker threads; 0 = use cores-1). */
    static void Init(unsigned nThreads = 0);

    /** Shutdown and join all worker threads. */
    static void Shutdown();

    /** Enqueue a job; returns a handle for Wait/IsDone. */
    static JobHandle Enqueue(std::function<void()> fn);

    /** Enqueue a job that belongs to the group; Wait(group) will wait for all. */
    static void EnqueueToGroup(JobGroup* group, std::function<void()> fn);

    /** Wait until the job identified by handle is done. Pumps jobs on this thread while waiting. */
    static void Wait(JobHandle handle);

    /** Wait until all jobs in the group are done. Pumps jobs on this thread while waiting. */
    static void Wait(JobGroup* group);

    /** True if the job has completed. */
    static bool IsDone(JobHandle handle);

    /** True if all jobs in the group have completed. */
    static bool IsDone(const JobGroup* group);
};

} // namespace engine::core
