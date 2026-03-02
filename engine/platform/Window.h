/**
 * @file Window.h
 * @brief Platform window abstraction (GLFW). Creation, resize/close callbacks, PollEvents.
 */

#pragma once

#include <functional>
#include <string>

namespace engine::platform {

/**
 * Window: create, poll events, resize/close callbacks.
 * GLFW is used internally; API isolates the implementation for future swap.
 */
class Window {
public:
    Window() = default;
    ~Window();

    /** Create window. Returns false on failure. */
    bool Create(const std::string& title, int width, int height);

    /** Destroy window and release resources. */
    void Destroy();

    /** Process OS events (call once per frame). */
    void PollEvents();

    /** True if window should close (e.g. user pressed close button). */
    bool ShouldClose() const;

    /** Current client width (updated on resize). */
    int GetWidth() const { return m_width; }

    /** Current client height (updated on resize). */
    int GetHeight() const { return m_height; }

    /** Raw handle for platform-specific use (e.g. Vulkan surface). */
    void* GetNativeHandle() const;

    /** Callback when window is resized (width, height). */
    using ResizeCallback = std::function<void(int, int)>;
    void SetResizeCallback(ResizeCallback cb) { m_onResize = std::move(cb); }

    /** Callback when window is requested to close. */
    using CloseCallback = std::function<void()>;
    void SetCloseCallback(CloseCallback cb) { m_onClose = std::move(cb); }

private:
    void* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    ResizeCallback m_onResize;
    CloseCallback m_onClose;
};

} // namespace engine::platform
