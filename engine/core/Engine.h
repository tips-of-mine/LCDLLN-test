/**
 * @file Engine.h
 * @brief Game loop: Run(), BeginFrame/Update/Render/EndFrame, double-buffer RenderState, hooks OnResize/OnQuit.
 */

#pragma once

#include "engine/core/Time.h"
#include <atomic>
#include <cstdint>
#include <functional>

namespace engine::platform {
class Window;
class Input;
}

namespace engine::core {

/** Minimal render state produced by Update and consumed by Render (double-buffered). */
struct RenderState {
    float view[16] = {};   /* column-major view matrix placeholder */
    float proj[16] = {};   /* column-major projection matrix placeholder */
    int drawListCount = 0; /* drawlist placeholder: number of items */
};

/**
 * Engine: Run() drives the loop. BeginFrame (FrameArena reset + input), Update (writes producer state),
 * Render (reads consumer state), EndFrame (swap). Atomic index for swap; no data race.
 */
class Engine {
public:
    Engine() = default;

    /** Run the main loop until quit. Uses the provided window and input; returns when window requests close. */
    void Run(platform::Window* window, platform::Input* input);

    /** Hook called when window is resized (width, height). */
    using ResizeCallback = std::function<void(int, int)>;
    void SetOnResize(ResizeCallback cb) { m_onResize = std::move(cb); }

    /** Hook called when quit is requested. */
    using QuitCallback = std::function<void()>;
    void SetOnQuit(QuitCallback cb) { m_onQuit = std::move(cb); }

private:
    void BeginFrame();
    void Update();
    void Render();
    void EndFrame();

    platform::Window* m_window = nullptr;
    platform::Input* m_input = nullptr;
    Time m_time{120};
    static constexpr int kRenderStateCount = 2;
    RenderState m_renderState[kRenderStateCount];
    std::atomic<int> m_readIndex{0};  /* consumer reads this index */
    bool m_vsync = true;
    ResizeCallback m_onResize;
    QuitCallback m_onQuit;
};

} // namespace engine::core
