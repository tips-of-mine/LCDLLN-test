// engine/core/jobs/JobSystem.cpp
// M00.3 — Job System implementation.
//
// Design (MVP):
//   - Single global MPSC-friendly deque guarded by one mutex + condvar.
//     Workers sleep on the condvar when the queue is empty.
//   - Each job is represented as a std::function<void()> + shared completion
//     counter (JobHandle::_counter). On job completion the counter is
//     decremented; waiting threads/workers re-check via condvar notification.
//   - Wait() loops: try to steal & execute one job, then sleep briefly until
//     done. This avoids deadlock when Wait() is called from a worker thread.
//   - JobGroup uses its own atomic counter, notified through the same pattern.

#include "JobSystem.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>

namespace engine::core::jobs {

// ---------------------------------------------------------------------------
// Internal structures
// ---------------------------------------------------------------------------

namespace {

/// A queued work item: the callable + optional completion counter to decrement.
struct Job {
    std::function<void()>           fn;
    /// May be null for jobs whose handle was discarded.
    std::shared_ptr<std::atomic<int>> counter;   // JobHandle counter
    std::atomic<int>*                 groupCtr;  // JobGroup counter (raw, lifetime managed by group)
    std::mutex*                       groupMtx;
    std::condition_variable*          groupCv;

    Job() : groupCtr(nullptr), groupMtx(nullptr), groupCv(nullptr) {}
};

// ---------------------------------------------------------------------------
// Global job queue
// ---------------------------------------------------------------------------

struct JobQueue {
    std::deque<Job>         items;
    std::mutex              mtx;
    std::condition_variable cv;    // workers wait here
    bool                    stop = false;

    /// Push one job (thread-safe).
    void Push(Job&& j) {
        {
            std::lock_guard<std::mutex> lk(mtx);
            items.push_back(std::move(j));
        }
        cv.notify_one();
    }

    /// Try to pop one job without blocking. Returns false if empty.
    bool TryPop(Job& out) {
        std::lock_guard<std::mutex> lk(mtx);
        if (items.empty()) return false;
        out = std::move(items.front());
        items.pop_front();
        return true;
    }

    /// Block until a job is available or stop is set.
    bool WaitPop(Job& out) {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [this]{ return stop || !items.empty(); });
        if (items.empty()) return false;
        out = std::move(items.front());
        items.pop_front();
        return true;
    }
};

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

static JobQueue               g_queue;
static std::vector<std::thread> g_workers;
static unsigned int           g_workerCount = 0;

// ---------------------------------------------------------------------------
// Execute one Job and signal completion
// ---------------------------------------------------------------------------

/// Run a job and update all completion counters.
static void ExecuteJob(Job& job) {
    if (job.fn) {
        job.fn();
    }

    // Signal JobHandle completion.
    if (job.counter) {
        const int prev = job.counter->fetch_sub(1, std::memory_order_acq_rel);
        (void)prev; // prev should be 1
    }

    // Signal JobGroup completion.
    if (job.groupCtr) {
        const int remaining = job.groupCtr->fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0 && job.groupMtx && job.groupCv) {
            // Wake threads waiting in JobGroup::Wait().
            std::lock_guard<std::mutex> lk(*job.groupMtx);
            job.groupCv->notify_all();
        }
    }
}

// ---------------------------------------------------------------------------
// Worker thread entry point
// ---------------------------------------------------------------------------

static void WorkerLoop() {
    Job job;
    while (true) {
        if (g_queue.WaitPop(job)) {
            ExecuteJob(job);
        } else {
            // stop was set and queue is empty — exit.
            break;
        }
    }
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// JobHandle
// ---------------------------------------------------------------------------

bool JobHandle::IsDone() const noexcept {
    if (!_counter) return true;
    return _counter->load(std::memory_order_acquire) == 0;
}

// ---------------------------------------------------------------------------
// JobGroup
// ---------------------------------------------------------------------------

void JobGroup::Add(std::function<void()> fn) {
    _pending.fetch_add(1, std::memory_order_relaxed);

    Job j;
    j.fn       = std::move(fn);
    j.groupCtr = &_pending;
    j.groupMtx = &_mtx;
    j.groupCv  = &_cv;

    g_queue.Push(std::move(j));
}

bool JobGroup::IsDone() const noexcept {
    return _pending.load(std::memory_order_acquire) == 0;
}

void JobGroup::Wait() {
    // Pump jobs from the global queue while waiting to avoid deadlock.
    // This is safe even when called from a worker thread.
    while (!IsDone()) {
        Job j;
        if (g_queue.TryPop(j)) {
            ExecuteJob(j);
        } else {
            // No jobs to steal — sleep briefly then retry.
            std::unique_lock<std::mutex> lk(_mtx);
            _cv.wait_for(lk, std::chrono::microseconds(50),
                [this]{ return IsDone(); });
        }
    }
}

// ---------------------------------------------------------------------------
// Jobs:: global API
// ---------------------------------------------------------------------------

namespace Jobs {

void Init(unsigned int numWorkers) {
    assert(g_workers.empty() && "Jobs::Init called twice");

    if (numWorkers == 0) {
        const unsigned int hw = std::thread::hardware_concurrency();
        numWorkers = (hw > 1u) ? hw - 1u : 1u;
    }
    numWorkers = std::max(numWorkers, 1u);

    g_workerCount    = numWorkers;
    g_queue.stop     = false;

    g_workers.reserve(numWorkers);
    for (unsigned int i = 0; i < numWorkers; ++i) {
        g_workers.emplace_back(WorkerLoop);
    }

    std::fprintf(stdout, "[Jobs] Initialised with %u worker threads\n", numWorkers);
}

void Shutdown() {
    // Signal all workers to stop.
    {
        std::lock_guard<std::mutex> lk(g_queue.mtx);
        g_queue.stop = true;
    }
    g_queue.cv.notify_all();

    // Join all workers.
    for (auto& t : g_workers) {
        if (t.joinable()) t.join();
    }
    g_workers.clear();
    g_workerCount = 0;

    std::fprintf(stdout, "[Jobs] Shutdown complete\n");
}

JobHandle Enqueue(std::function<void()> fn) {
    auto counter = std::make_shared<std::atomic<int>>(1);

    Job j;
    j.fn      = std::move(fn);
    j.counter = counter;
    // No group counters for stand-alone handles.

    g_queue.Push(std::move(j));

    JobHandle h;
    h._counter = std::move(counter);
    return h;
}

void Wait(JobHandle handle) {
    // Pump jobs from the global queue while the handle is not done.
    while (!handle.IsDone()) {
        Job j;
        if (g_queue.TryPop(j)) {
            ExecuteJob(j);
        } else {
            // Brief yield to avoid busy-spinning.
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }
}

unsigned int WorkerCount() noexcept {
    return g_workerCount;
}

} // namespace Jobs

} // namespace engine::core::jobs
