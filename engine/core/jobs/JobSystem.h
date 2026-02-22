#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace Jobs {

struct JobHandle;

struct JobGroup {
    std::atomic<int32_t> count{0};

    [[nodiscard]] bool IsDone() const noexcept {
        return count.load(std::memory_order_acquire) == 0;
    }
};

struct JobHandle {
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;

    uint32_t index = kInvalidIndex;
    uint32_t generation = 0;

    [[nodiscard]] bool IsDone() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept { return index != kInvalidIndex; }
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

namespace detail {
constexpr size_t kJobFunctionStorageSize = 64;

struct JobFunction {
    using Invoker = void (*)(void*);
    using Destroyer = void (*)(void*);
    using Mover = void (*)(void* dst, void* src);

    alignas(std::max_align_t) unsigned char storage[kJobFunctionStorageSize] = {};
    Invoker invoke = nullptr;
    Destroyer destroy = nullptr;
    Mover move = nullptr;

    JobFunction() = default;
    JobFunction(const JobFunction&) = delete;
    JobFunction& operator=(const JobFunction&) = delete;

    JobFunction(JobFunction&& other) noexcept {
        *this = std::move(other);
    }

    JobFunction& operator=(JobFunction&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        Reset();
        invoke = other.invoke;
        destroy = other.destroy;
        move = other.move;
        if (move) {
            move(storage, other.storage);
            other.invoke = nullptr;
            other.destroy = nullptr;
            other.move = nullptr;
        }
        return *this;
    }

    ~JobFunction() { Reset(); }

    template <typename F>
    void Set(F&& fn) {
        using T = std::decay_t<F>;
        static_assert(sizeof(T) <= kJobFunctionStorageSize, "Job callable too large for inline storage");
        Reset();
        new (storage) T(std::forward<F>(fn));
        invoke = [](void* data) { (*reinterpret_cast<T*>(data))(); };
        destroy = [](void* data) { reinterpret_cast<T*>(data)->~T(); };
        move = [](void* dst, void* src) {
            new (dst) T(std::move(*reinterpret_cast<T*>(src)));
            reinterpret_cast<T*>(src)->~T();
        };
    }

    void Execute() {
        if (invoke) {
            invoke(storage);
        }
    }

    void Reset() {
        if (destroy) {
            destroy(storage);
        }
        invoke = nullptr;
        destroy = nullptr;
        move = nullptr;
    }
};

JobHandle EnqueueJob(JobFunction&& function, JobGroup* group);
} // namespace detail

} // namespace Jobs

#include "JobSystem.inl"
