#pragma once

/**
 * @file Profiler.h
 * @brief CPU profiler: PROFILE_SCOPE macros and ring-buffer events (M18.1).
 *
 * Scopes are recorded per thread; EndFrame() swaps buffers so the last
 * completed frame's data is available for the debug overlay.
 */

#include <cstdint>
#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace engine::core {

/** @brief Maximum length of a scope name stored in the ring buffer. */
constexpr size_t kProfilerScopeNameMax = 64u;

/** @brief Maximum number of scope events per frame in the ring buffer. */
constexpr size_t kProfilerRingBufferSize = 4096u;

/**
 * @brief Single scope event: name, thread id, start and end ticks (nanoseconds).
 */
struct ProfilerScopeEvent {
    char     name[kProfilerScopeNameMax]{};
    uint32_t threadId = 0u;
    uint64_t startNs  = 0u;
    uint64_t endNs    = 0u;
};

/**
 * @brief CPU profiler: ring buffer of scope events, frame swap, optional export.
 */
class Profiler {
public:
    Profiler() = default;
    ~Profiler() = default;

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    /**
     * @brief Called at the start of a scope (e.g. from PROFILE_SCOPE macro).
     *
     * @param name Scope name (truncated to kProfilerScopeNameMax - 1).
     */
    void BeginScope(std::string_view name);

    /**
     * @brief Called at the end of a scope (e.g. from scope guard destructor).
     */
    void EndScope();

    /**
     * @brief Call at end of frame: swaps current buffer to "last frame" for reading.
     */
    void EndFrame();

    /**
     * @brief Returns the last completed frame's total CPU time in milliseconds.
     *
     * Uses the "Frame" scope if present, otherwise (max end - min start) of events.
     *
     * @return Frame duration in ms, or 0.f if no data.
     */
    [[nodiscard]] float GetLastFrameCpuMs() const noexcept;

    /**
     * @brief Returns the last completed frame's scope events for overlay/export.
     *
     * Thread-safe: returns a copy. Call from main thread after EndFrame().
     *
     * @return Copy of scope events (may be empty).
     */
    [[nodiscard]] std::vector<ProfilerScopeEvent> GetLastFrameScopes() const;

    /**
     * @brief Returns the global profiler instance (used by PROFILE_SCOPE).
     */
    static Profiler& Instance();

private:
    using Clock = std::chrono::steady_clock;

    std::vector<ProfilerScopeEvent> m_ringBuffer;
    size_t                          m_ringHead = 0u;
    size_t                          m_ringCount = 0u;
    mutable std::mutex              m_mutex;

    std::vector<ProfilerScopeEvent> m_lastFrameScopes;
    float                          m_lastFrameCpuMs = 0.f;

    void FlushCurrentToLast();
};

/**
 * @brief RAII guard that calls BeginScope on ctor and EndScope on dtor.
 */
class ProfilerScopeGuard {
public:
    ProfilerScopeGuard(Profiler& profiler, std::string_view name)
        : m_profiler(profiler) {
        m_profiler.BeginScope(name);
    }
    ~ProfilerScopeGuard() {
        m_profiler.EndScope();
    }
    ProfilerScopeGuard(const ProfilerScopeGuard&) = delete;
    ProfilerScopeGuard& operator=(const ProfilerScopeGuard&) = delete;

private:
    Profiler& m_profiler;
};

} // namespace engine::core

/**
 * @brief Macro: profile the current scope (CPU). Name must be a string literal or string_view.
 */
#define PROFILE_SCOPE(name) \
    ::engine::core::ProfilerScopeGuard _profile_guard_##__LINE__(::engine::core::Profiler::Instance(), (name))
