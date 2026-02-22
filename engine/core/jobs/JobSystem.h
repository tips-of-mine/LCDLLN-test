#pragma once

#include <atomic>
#include <cstdint>

namespace Jobs {

struct JobHandle;

struct JobGroup {
    std::atomic<int32_t> count{0};

    [[nodiscard]] bool IsDone() const noexcept {
        return count.load(std::memory_order_acquire) == 0;
    }
};

struct JobHandle {
    uint32_t index = 0;
    uint32_t generation = 0;

    [[nodiscard]] bool IsDone() const noexcept;
};

void Init(uint32_t workerCount = 0);
void Shutdown();
[[nodiscard]] bool IsInitialized() noexcept;

JobHandle Enqueue(void (*fn)());

template <typename F>
JobHandle Enqueue(F&& fn);

template <typename F>
JobHandle Enqueue(JobGroup& group, F&& fn);

void Wait(const JobHandle& handle);
void Wait(JobGroup& group);

bool IsDone(const JobHandle& handle) noexcept;

} // namespace Jobs

#include "JobSystem.inl"
