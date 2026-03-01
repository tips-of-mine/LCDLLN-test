/**
 * @file Profiler.cpp
 * @brief CPU profiler implementation: ring buffer events, frame swap (M18.1).
 */

#include "engine/core/Profiler.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>

namespace engine::core {

namespace {

uint32_t GetCurrentThreadId() {
    static thread_local uint32_t s_tid = 0u;
    static std::atomic<uint32_t> s_nextTid{1u};
    if (s_tid == 0u) {
        s_tid = s_nextTid.fetch_add(1u, std::memory_order_relaxed);
    }
    return s_tid;
}

uint64_t NowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Profiler::Clock::now().time_since_epoch()).count());
}

} // namespace

void Profiler::BeginScope(std::string_view name) {
    std::lock_guard lock(m_mutex);
    if (m_ringBuffer.size() < kProfilerRingBufferSize) {
        m_ringBuffer.resize(kProfilerRingBufferSize);
    }
    ProfilerScopeEvent e;
    e.threadId = GetCurrentThreadId();
    e.startNs  = NowNs();
    e.endNs    = 0u;
    size_t len = (std::min)(name.size(), kProfilerScopeNameMax - 1u);
    std::memcpy(e.name, name.data(), len);
    e.name[len] = '\0';
    size_t idx = (m_ringHead + m_ringCount) % kProfilerRingBufferSize;
    m_ringBuffer[idx] = e;
    if (m_ringCount < kProfilerRingBufferSize) {
        ++m_ringCount;
    } else {
        m_ringHead = (m_ringHead + 1u) % kProfilerRingBufferSize;
    }
}

void Profiler::EndScope() {
    const uint64_t endNs = NowNs();
    const uint32_t tid   = GetCurrentThreadId();
    std::lock_guard lock(m_mutex);
    for (size_t i = 0; i < m_ringCount; ++i) {
        size_t idx = (m_ringHead + m_ringCount - 1u - i) % kProfilerRingBufferSize;
        if (m_ringBuffer[idx].threadId == tid && m_ringBuffer[idx].endNs == 0u) {
            m_ringBuffer[idx].endNs = endNs;
            return;
        }
    }
}

void Profiler::EndFrame() {
    std::lock_guard lock(m_mutex);
    FlushCurrentToLast();
}

void Profiler::FlushCurrentToLast() {
    m_lastFrameScopes.clear();
    m_lastFrameCpuMs = 0.f;
    for (size_t i = 0; i < m_ringCount; ++i) {
        size_t idx = (m_ringHead + i) % kProfilerRingBufferSize;
        const ProfilerScopeEvent& e = m_ringBuffer[idx];
        if (e.endNs > e.startNs) {
            m_lastFrameScopes.push_back(e);
        }
    }
    m_ringHead  = 0u;
    m_ringCount = 0u;

    if (m_lastFrameScopes.empty()) {
        return;
    }
    uint64_t minStart = m_lastFrameScopes.front().startNs;
    uint64_t maxEnd  = m_lastFrameScopes.front().endNs;
    for (const auto& e : m_lastFrameScopes) {
        minStart = (std::min)(minStart, e.startNs);
        maxEnd   = (std::max)(maxEnd, e.endNs);
    }
    for (const auto& e : m_lastFrameScopes) {
        if (std::strcmp(e.name, "Frame") == 0) {
            m_lastFrameCpuMs = (e.endNs - e.startNs) / 1e6f;
            return;
        }
    }
    m_lastFrameCpuMs = (maxEnd - minStart) / 1e6f;
}

float Profiler::GetLastFrameCpuMs() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_lastFrameCpuMs;
}

std::vector<ProfilerScopeEvent> Profiler::GetLastFrameScopes() const {
    std::lock_guard lock(m_mutex);
    return m_lastFrameScopes;
}

Profiler& Profiler::Instance() {
    static Profiler s_instance;
    return s_instance;
}

} // namespace engine::core
