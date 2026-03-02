/**
 * @file JobSystem.cpp
 * @brief Thread pool, mutex/condvar queue, slot pool, Wait pumps jobs to avoid deadlock.
 */

#include "engine/core/jobs/JobSystem.h"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace engine::core {

namespace {

constexpr uint32_t kMaxJobSlots = 128 * 1024;
constexpr uint32_t kMaxCounters = 128 * 1024;

struct JobSlot {
    std::function<void()> fn;
};

std::vector<JobSlot> g_slots;
std::vector<uint32_t> g_freeSlots;
std::vector<std::atomic<uint32_t>> g_counters;
std::vector<uint32_t> g_freeCounters;
std::mutex g_counterMutex;
std::deque<uint32_t> g_queue;
std::mutex g_queueMutex;
std::condition_variable g_queueCond;
std::atomic<bool> g_stop{false};
std::vector<std::thread> g_workers;
bool g_initialized = false;

uint32_t AllocCounter() {
    std::lock_guard<std::mutex> lock(g_counterMutex);
    if (g_freeCounters.empty()) return JobHandle::kInvalid;
    uint32_t c = g_freeCounters.back();
    g_freeCounters.pop_back();
    return c;
}

void FreeCounter(uint32_t c) {
    if (c >= g_counters.size()) return;
    std::lock_guard<std::mutex> lock(g_counterMutex);
    g_freeCounters.push_back(c);
}

uint32_t AllocSlot() {
    uint32_t i;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (g_freeSlots.empty()) return JobHandle::kInvalid;
        i = g_freeSlots.back();
        g_freeSlots.pop_back();
    }
    return i;
}

void FreeSlot(uint32_t i) {
    if (i >= g_slots.size()) return;
    g_slots[i].fn = nullptr;
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_freeSlots.push_back(i);
}

bool TryRunOneJob() {
    uint32_t slotIndex = JobHandle::kInvalid;
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (g_queue.empty()) return false;
        slotIndex = g_queue.front();
        g_queue.pop_front();
    }
    if (slotIndex >= g_slots.size()) return true;
    JobSlot& slot = g_slots[slotIndex];
    if (slot.fn) {
        slot.fn();
        slot.fn = nullptr;
    }
    FreeSlot(slotIndex);
    return true;
}

void WorkerLoop() {
    while (true) {
        uint32_t slotIndex = JobHandle::kInvalid;
        {
            std::unique_lock<std::mutex> lock(g_queueMutex);
            g_queueCond.wait(lock, [] { return g_stop.load() || !g_queue.empty(); });
            if (g_stop.load() && g_queue.empty()) return;
            if (!g_queue.empty()) {
                slotIndex = g_queue.front();
                g_queue.pop_front();
            }
        }
        if (slotIndex == JobHandle::kInvalid) continue;
        if (slotIndex >= g_slots.size()) continue;
        JobSlot& slot = g_slots[slotIndex];
        if (slot.fn) {
            slot.fn();
            slot.fn = nullptr;
        }
        FreeSlot(slotIndex);
    }
}

} // namespace

void Jobs::Init(unsigned nThreads) {
    if (g_initialized) return;
    if (nThreads == 0) {
        nThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }
    g_slots.resize(kMaxJobSlots);
    g_freeSlots.reserve(kMaxJobSlots);
    for (uint32_t i = 0; i < kMaxJobSlots; ++i) g_freeSlots.push_back(i);
    g_counters.resize(kMaxCounters);
    g_freeCounters.reserve(kMaxCounters);
    for (uint32_t i = 0; i < kMaxCounters; ++i) {
        g_counters[i].store(0, std::memory_order_relaxed);
        g_freeCounters.push_back(i);
    }
    g_stop.store(false);
    g_workers.reserve(nThreads);
    for (unsigned i = 0; i < nThreads; ++i) {
        g_workers.emplace_back(WorkerLoop);
    }
    g_initialized = true;
}

void Jobs::Shutdown() {
    if (!g_initialized) return;
    g_stop.store(true);
    g_queueCond.notify_all();
    for (auto& t : g_workers) {
        if (t.joinable()) t.join();
    }
    g_workers.clear();
    g_slots.clear();
    g_freeSlots.clear();
    g_counters.clear();
    g_freeCounters.clear();
    g_queue.clear();
    g_initialized = false;
}

JobHandle Jobs::Enqueue(std::function<void()> fn) {
    if (!fn || !g_initialized) return JobHandle();
    uint32_t c = AllocCounter();
    if (c == JobHandle::kInvalid) return JobHandle();
    g_counters[c].store(1, std::memory_order_release);
    uint32_t i = AllocSlot();
    if (i == JobHandle::kInvalid) {
        g_counters[c].store(0, std::memory_order_release);
        FreeCounter(c);
        return JobHandle();
    }
    std::function<void()> wrapped = [userFn = std::move(fn), c]() {
        userFn();
        g_counters[c].fetch_sub(1, std::memory_order_release);
    };
    g_slots[i].fn = std::move(wrapped);
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_queue.push_back(i);
    }
    g_queueCond.notify_one();
    return JobHandle(c);
}

void Jobs::EnqueueToGroup(JobGroup* group, std::function<void()> fn) {
    if (!group || !fn || !g_initialized) return;
    group->m_pending.fetch_add(1, std::memory_order_relaxed);
    std::function<void()> wrapped = [fn, group]() {
        fn();
        group->m_pending.fetch_sub(1, std::memory_order_release);
    };
    uint32_t i = AllocSlot();
    if (i == JobHandle::kInvalid) {
        group->m_pending.fetch_sub(1, std::memory_order_relaxed);
        return;
    }
    g_slots[i].fn = std::move(wrapped);
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        g_queue.push_back(i);
    }
    g_queueCond.notify_one();
}

void Jobs::Wait(JobHandle handle) {
    if (!handle.Valid()) return;
    uint32_t c = handle.m_counterIndex;
    while (g_counters[c].load(std::memory_order_acquire) != 0) {
        TryRunOneJob();
    }
    FreeCounter(c);
}

void Jobs::Wait(JobGroup* group) {
    if (!group) return;
    while (!group->IsDone()) {
        TryRunOneJob();
    }
}

bool Jobs::IsDone(JobHandle handle) {
    if (!handle.Valid()) return true;
    return g_counters[handle.m_counterIndex].load(std::memory_order_acquire) == 0;
}

bool Jobs::IsDone(const JobGroup* group) {
    return group ? group->IsDone() : true;
}

} // namespace engine::core
