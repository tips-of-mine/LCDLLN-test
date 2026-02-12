#include "engine/core/jobs/JobSystem.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace engine::core::jobs {
namespace {

constexpr std::uint32_t kInvalidIndex = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t kDefaultMaxJobs = 262144;

thread_local int g_workerIndex = -1;

struct CounterState {
    std::atomic<std::uint32_t> pending{0};
    std::atomic<bool> active{false};
    std::uint32_t generation = 0;
};

struct JobEntry {
    Jobs::JobCallableOps ops;
    alignas(std::max_align_t) std::array<std::byte, Jobs::kMaxJobCallableSize> storage{};
    std::uint32_t callableSize = 0;
    JobGroup* group = nullptr;
    std::uint32_t counterIndex = kInvalidIndex;
    std::uint32_t counterGeneration = 0;
    bool hasHandleCounter = false;
};

class WorkQueue {
public:
    void Push(std::uint32_t jobIndex) {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(jobIndex);
    }

    bool TryPop(std::uint32_t& outJobIndex) {
        std::lock_guard lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        outJobIndex = m_queue.back();
        m_queue.pop_back();
        return true;
    }

    bool TrySteal(std::uint32_t& outJobIndex) {
        std::lock_guard lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        outJobIndex = m_queue.front();
        m_queue.pop_front();
        return true;
    }

private:
    std::mutex m_mutex;
    std::deque<std::uint32_t> m_queue;
};

class JobSystemState {
public:
    void Init(std::size_t workerCount) {
        Shutdown();

        if (workerCount == 0) {
            const std::size_t hw = std::max<std::size_t>(1, std::thread::hardware_concurrency());
            workerCount = (hw > 1) ? hw - 1 : 1;
        }

        {
            std::lock_guard lock(m_jobPoolMutex);
            m_jobPool.clear();
            m_jobPool.resize(kDefaultMaxJobs);
            m_jobFreeList.clear();
            m_jobFreeList.reserve(kDefaultMaxJobs);
            for (std::uint32_t i = 0; i < m_jobPool.size(); ++i) {
                m_jobFreeList.push_back(i);
            }
        }

        {
            std::lock_guard lock(m_counterPoolMutex);
            m_counterCapacity = static_cast<std::uint32_t>(kDefaultMaxJobs);
            m_counters = std::make_unique<CounterState[]>(m_counterCapacity);
            m_counterFreeList.clear();
            m_counterFreeList.reserve(m_counterCapacity);
            for (std::uint32_t i = 0; i < m_counterCapacity; ++i) {
                m_counterFreeList.push_back(i);
            }
        }

        m_queues.clear();
        m_queues.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) {
            m_queues.push_back(std::make_unique<WorkQueue>());
        }
        m_stopRequested.store(false, std::memory_order_release);
        m_nextQueue.store(0, std::memory_order_relaxed);

        m_workers.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) {
            m_workers.emplace_back([this, i]() { WorkerLoop(static_cast<int>(i)); });
        }

        m_initialized.store(true, std::memory_order_release);
    }

    void Shutdown() {
        const bool wasInitialized = m_initialized.exchange(false, std::memory_order_acq_rel);
        if (!wasInitialized) {
            return;
        }

        m_stopRequested.store(true, std::memory_order_release);
        m_workAvailableCv.notify_all();

        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        m_workers.clear();
        m_queues.clear();

        {
            std::lock_guard lock(m_jobPoolMutex);
            m_jobPool.clear();
            m_jobFreeList.clear();
        }

        {
            std::lock_guard lock(m_counterPoolMutex);
            m_counters.reset();
            m_counterCapacity = 0;
            m_counterFreeList.clear();
        }
    }

    JobHandle Enqueue(JobGroup* group,
                      Jobs::JobCallableOps ops,
                      const void* callableStorage,
                      std::size_t callableSize) {
        assert(m_initialized.load(std::memory_order_acquire) && "Jobs::Init must be called before Enqueue");

        const std::uint32_t counterIndex = AcquireCounter();
        CounterState& counter = m_counters[counterIndex];
        const std::uint32_t generation = counter.generation;
        counter.pending.store(1, std::memory_order_release);
        counter.active.store(true, std::memory_order_release);

        if (group != nullptr) {
            group->m_pending.fetch_add(1, std::memory_order_acq_rel);
        }

        const std::uint32_t jobIndex = AcquireJobSlot();
        {
            std::lock_guard lock(m_jobPoolMutex);
            JobEntry& entry = m_jobPool[jobIndex];
            entry.ops = ops;
            entry.callableSize = static_cast<std::uint32_t>(callableSize);
            std::memcpy(entry.storage.data(), callableStorage, callableSize);
            entry.group = group;
            entry.counterIndex = counterIndex;
            entry.counterGeneration = generation;
            entry.hasHandleCounter = true;
        }

        const std::size_t queueCount = m_queues.size();
        const std::size_t queueIndex = m_nextQueue.fetch_add(1, std::memory_order_relaxed) % queueCount;
        m_queues[queueIndex]->Push(jobIndex);
        m_workAvailableCv.notify_one();

        JobHandle handle;
        handle.m_counterIndex = counterIndex;
        handle.m_generation = generation;
        handle.m_valid = true;
        return handle;
    }

    bool IsDone(const JobHandle& handle) {
        if (!handle.m_valid) {
            return true;
        }

        CounterState* state = GetCounter(handle);
        if (state == nullptr) {
            return true;
        }

        return state->pending.load(std::memory_order_acquire) == 0;
    }

    bool IsDone(const JobGroup& group) const {
        return group.m_pending.load(std::memory_order_acquire) == 0;
    }

    void Wait(const JobHandle& handle) {
        if (!handle.m_valid) {
            return;
        }

        CounterState* state = GetCounter(handle);
        if (state == nullptr) {
            return;
        }

        WaitUntil([state]() {
            return state->pending.load(std::memory_order_acquire) == 0;
        });

        ReleaseCounterIfActive(handle.m_counterIndex, handle.m_generation);
    }

    void Wait(JobGroup& group) {
        WaitUntil([&group]() {
            return group.m_pending.load(std::memory_order_acquire) == 0;
        });
    }

private:
    CounterState* GetCounter(const JobHandle& handle) {
        if (handle.m_counterIndex >= m_counterCapacity) {
            return nullptr;
        }

        CounterState& state = m_counters[handle.m_counterIndex];
        if (state.generation != handle.m_generation || !state.active.load(std::memory_order_acquire)) {
            return nullptr;
        }
        return &state;
    }

    std::uint32_t AcquireCounter() {
        std::lock_guard lock(m_counterPoolMutex);
        assert(!m_counterFreeList.empty() && "Counter pool exhausted");
        const std::uint32_t index = m_counterFreeList.back();
        m_counterFreeList.pop_back();
        CounterState& state = m_counters[index];
        state.generation++;
        return index;
    }

    void ReleaseCounterIfActive(std::uint32_t counterIndex, std::uint32_t generation) {
        std::lock_guard lock(m_counterPoolMutex);
        if (counterIndex >= m_counterCapacity) {
            return;
        }

        CounterState& state = m_counters[counterIndex];
        if (state.generation != generation) {
            return;
        }

        const bool wasActive = state.active.exchange(false, std::memory_order_acq_rel);
        if (wasActive) {
            m_counterFreeList.push_back(counterIndex);
        }
    }

    std::uint32_t AcquireJobSlot() {
        std::lock_guard lock(m_jobPoolMutex);
        assert(!m_jobFreeList.empty() && "Job pool exhausted");
        const std::uint32_t index = m_jobFreeList.back();
        m_jobFreeList.pop_back();
        return index;
    }

    void ReleaseJobSlot(std::uint32_t index) {
        std::lock_guard lock(m_jobPoolMutex);
        m_jobFreeList.push_back(index);
    }

    bool TryPopJob(std::uint32_t& outJob) {
        if (m_queues.empty()) {
            return false;
        }

        const int workerIndex = g_workerIndex;
        if (workerIndex >= 0 && static_cast<std::size_t>(workerIndex) < m_queues.size()) {
            if (m_queues[workerIndex]->TryPop(outJob)) {
                return true;
            }

            for (std::size_t i = 0; i < m_queues.size(); ++i) {
                if (i == static_cast<std::size_t>(workerIndex)) {
                    continue;
                }
                if (m_queues[i]->TrySteal(outJob)) {
                    return true;
                }
            }
            return false;
        }

        for (std::size_t i = 0; i < m_queues.size(); ++i) {
            if (m_queues[i]->TrySteal(outJob)) {
                return true;
            }
        }

        return false;
    }

    void RunOneJob(std::uint32_t jobIndex) {
        JobEntry entry;
        {
            std::lock_guard lock(m_jobPoolMutex);
            entry = m_jobPool[jobIndex];
        }

        entry.ops.invoke(entry.storage.data());
        entry.ops.destroy(entry.storage.data());

        if (entry.group != nullptr) {
            entry.group->m_pending.fetch_sub(1, std::memory_order_acq_rel);
        }

        if (entry.hasHandleCounter && entry.counterIndex < m_counterCapacity) {
            CounterState& state = m_counters[entry.counterIndex];
            if (state.generation == entry.counterGeneration) {
                state.pending.fetch_sub(1, std::memory_order_acq_rel);
            }
        }

        ReleaseJobSlot(jobIndex);
        m_workAvailableCv.notify_all();
    }

    template <typename Predicate>
    void WaitUntil(Predicate done) {
        while (!done()) {
            std::uint32_t jobIndex = kInvalidIndex;
            if (TryPopJob(jobIndex)) {
                RunOneJob(jobIndex);
                continue;
            }

            std::unique_lock lock(m_waitMutex);
            m_workAvailableCv.wait_for(lock, std::chrono::microseconds(200));
        }
    }

    void WorkerLoop(int workerIndex) {
        g_workerIndex = workerIndex;

        while (!m_stopRequested.load(std::memory_order_acquire)) {
            std::uint32_t jobIndex = kInvalidIndex;
            if (TryPopJob(jobIndex)) {
                RunOneJob(jobIndex);
                continue;
            }

            std::unique_lock lock(m_waitMutex);
            m_workAvailableCv.wait_for(lock, std::chrono::milliseconds(1));
        }

        g_workerIndex = -1;
    }

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<std::size_t> m_nextQueue{0};

    std::vector<std::thread> m_workers;
    std::vector<std::unique_ptr<WorkQueue>> m_queues;

    std::vector<JobEntry> m_jobPool;
    std::vector<std::uint32_t> m_jobFreeList;
    std::mutex m_jobPoolMutex;

    std::unique_ptr<CounterState[]> m_counters;
    std::uint32_t m_counterCapacity = 0;
    std::vector<std::uint32_t> m_counterFreeList;
    std::mutex m_counterPoolMutex;

    std::mutex m_waitMutex;
    std::condition_variable m_workAvailableCv;
};

JobSystemState g_state;

} // namespace

bool JobGroup::IsDone() const {
    return Jobs::IsDone(*this);
}

void JobGroup::Wait() {
    Jobs::Wait(*this);
}

bool JobHandle::IsDone() const {
    return Jobs::IsDone(*this);
}

void JobHandle::Wait() {
    Jobs::Wait(*this);
}

void Jobs::Init(std::size_t workerCount) {
    g_state.Init(workerCount);
}

void Jobs::Shutdown() {
    g_state.Shutdown();
}

JobHandle Jobs::EnqueueErased(JobGroup* group,
                              JobCallableOps ops,
                              const void* callableStorage,
                              std::size_t callableSize) {
    return g_state.Enqueue(group, ops, callableStorage, callableSize);
}

void Jobs::Wait(const JobHandle& handle) {
    g_state.Wait(handle);
}

void Jobs::Wait(JobGroup& group) {
    g_state.Wait(group);
}

bool Jobs::IsDone(const JobHandle& handle) {
    return g_state.IsDone(handle);
}

bool Jobs::IsDone(const JobGroup& group) {
    return g_state.IsDone(group);
}

} // namespace engine::core::jobs
