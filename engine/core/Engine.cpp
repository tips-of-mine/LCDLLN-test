/**
 * @file Engine.cpp
 * @brief Game loop: BeginFrame (FrameArena + input), Update (producer), Render (consumer), EndFrame (swap). FPS log, OnResize/OnQuit hooks.
 */

#include "engine/core/Engine.h"
#include "engine/core/Log.h"
#include "engine/core/Config.h"
#include "engine/core/memory/Memory.h"
#include "engine/platform/Window.h"
#include "engine/platform/Input.h"
#include <chrono>

namespace engine::core {

void Engine::Run(platform::Window* window, platform::Input* input) {
    if (!window || !input) return;
    m_window = window;
    m_input = input;
    m_vsync = Config::GetBool("vsync", true);
    m_readIndex.store(0, std::memory_order_relaxed);

    window->SetResizeCallback([this](int w, int h) {
        if (m_onResize) m_onResize(w, h);
    });

    auto lastFpsLog = std::chrono::steady_clock::now();
    const auto oneSec = std::chrono::seconds(1);

    /* Cadre pour fixed tick (option) et frame pacing: étendre BeginFrame ou la boucle si besoin. */
    while (!window->ShouldClose()) {
        window->PollEvents();
        BeginFrame();
        Update();
        Render();
        EndFrame();

        auto now = std::chrono::steady_clock::now();
        if (now - lastFpsLog >= oneSec) {
            LOG_INFO(Core, "FPS: %.1f frameIndex: %llu", m_time.Fps(), static_cast<unsigned long long>(m_time.FrameIndex()));
            lastFpsLog = now;
        }
    }

    if (m_onQuit) m_onQuit();
    m_window = nullptr;
    m_input = nullptr;
}

void Engine::BeginFrame() {
    m_time.BeginFrame();
    Memory::BeginFrame(m_time.FrameIndex());
    if (m_input && m_window)
        m_input->Update(m_window);
}

void Engine::Update() {
    int readIdx = m_readIndex.load(std::memory_order_acquire);
    int writeIdx = 1 - readIdx;
    RenderState& state = m_renderState[writeIdx];
    state.drawListCount = 0;
    (void)state.view;
    (void)state.proj;
}

void Engine::Render() {
    int readIdx = m_readIndex.load(std::memory_order_acquire);
    RenderState const& state = m_renderState[readIdx];
    (void)state;
}

void Engine::EndFrame() {
    m_time.EndFrame();
    m_readIndex.store(1 - m_readIndex.load(std::memory_order_relaxed), std::memory_order_release);
}

} // namespace engine::core
