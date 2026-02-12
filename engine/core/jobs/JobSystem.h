#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace engine::core::jobs {

class Jobs;

class JobGroup {
public:
    JobGroup() = default;

    JobGroup(const JobGroup&) = delete;
    JobGroup& operator=(const JobGroup&) = delete;

    [[nodiscard]] bool IsDone() const;
    void Wait();

    std::atomic<std::uint32_t> m_pending{0};
};

class JobHandle {
public:
    [[nodiscard]] bool IsDone() const;
    void Wait();

    std::uint32_t m_counterIndex = 0;
    std::uint32_t m_generation = 0;
    bool m_valid = false;
};

class Jobs {
public:
    static constexpr std::size_t kMaxJobCallableSize = 128;

    struct JobCallableOps {
        void (*invoke)(void* storage) = nullptr;
        void (*destroy)(void* storage) = nullptr;
    };

    static void Init(std::size_t workerCount = 0);
    static void Shutdown();

    template <typename Fn>
    static JobHandle Enqueue(Fn&& fn) {
        return EnqueueTyped<std::decay_t<Fn>>(nullptr, std::forward<Fn>(fn));
    }

    template <typename Fn>
    static JobHandle Enqueue(JobGroup& group, Fn&& fn) {
        return EnqueueTyped<std::decay_t<Fn>>(&group, std::forward<Fn>(fn));
    }

    static void Wait(const JobHandle& handle);
    static void Wait(JobGroup& group);

    static bool IsDone(const JobHandle& handle);
    static bool IsDone(const JobGroup& group);

private:
    template <typename Callable, typename Fn>
    static JobHandle EnqueueTyped(JobGroup* group, Fn&& fn) {
        static_assert(sizeof(Callable) <= kMaxJobCallableSize, "Job callable too large for in-place storage");
        static_assert(alignof(Callable) <= alignof(std::max_align_t), "Job callable alignment too strict");

        JobCallableOps ops;
        ops.invoke = [](void* storage) {
            (*reinterpret_cast<Callable*>(storage))();
        };
        ops.destroy = [](void* storage) {
            reinterpret_cast<Callable*>(storage)->~Callable();
        };

        alignas(std::max_align_t) std::byte storage[kMaxJobCallableSize]{};
        new (storage) Callable(std::forward<Fn>(fn));
        return EnqueueErased(group, ops, storage, sizeof(Callable));
    }

    static JobHandle EnqueueErased(JobGroup* group,
                                   JobCallableOps ops,
                                   const void* callableStorage,
                                   std::size_t callableSize);
};

} // namespace engine::core::jobs
