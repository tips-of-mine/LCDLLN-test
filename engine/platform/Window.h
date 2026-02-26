#pragma once

/**
 * @file Window.h
 * @brief GLFW-based window abstraction for the engine platform layer.
 *
 * Isolates GLFW behind a clean interface so the rest of the engine never
 * includes GLFW headers directly.  All public methods are called from the
 * main (game-loop) thread.
 *
 * Typical usage:
 *   Window win;
 *   win.Init(1280, 720, "My Game");
 *   while (!win.ShouldClose()) {
 *       win.PollEvents();
 *       // ... render ...
 *       win.SwapBuffers();   // only needed for OpenGL; Vulkan manages its own swap
 *   }
 *   win.Shutdown();
 */

#include <cstdint>
#include <functional>
#include <string_view>

// Forward-declare the opaque GLFW window type so that Window.h
// does not expose GLFW headers to every translation unit.
struct GLFWwindow;

namespace engine::platform {

/// Fired whenever the framebuffer is resized.
using ResizeCallback = std::function<void(int width, int height)>;

/// Fired when the user requests window close (e.g. clicks the ×).
using CloseCallback  = std::function<void()>;

/**
 * @brief Manages a single OS window through GLFW.
 *
 * Only one Window instance should be active at a time (GLFW is a global
 * subsystem).  The class is non-copyable and non-movable.
 */
class Window {
public:
    Window()  = default;
    ~Window() = default;

    // Non-copyable, non-movable.
    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&)                 = delete;
    Window& operator=(Window&&)      = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /**
     * @brief Initialises GLFW, creates the window, and installs callbacks.
     *
     * GLFW is initialised here; call Shutdown() to terminate it.
     * The function calls LOG_FATAL on any GLFW error.
     *
     * @param width    Framebuffer width in pixels.
     * @param height   Framebuffer height in pixels.
     * @param title    Window title.
     * @param fullscreen  If true, creates a fullscreen window on the primary monitor.
     */
    void Init(int width, int height, std::string_view title, bool fullscreen = false);

    /**
     * @brief Destroys the window and terminates GLFW.
     *
     * Safe to call even if Init() was never called.
     */
    void Shutdown();

    // -----------------------------------------------------------------------
    // Per-frame
    // -----------------------------------------------------------------------

    /**
     * @brief Processes OS events and dispatches registered callbacks.
     *
     * Must be called once per frame from the main thread.
     */
    void PollEvents();

    // -----------------------------------------------------------------------
    // State queries
    // -----------------------------------------------------------------------

    /// Returns true when the window has received a close request.
    [[nodiscard]] bool ShouldClose() const noexcept;

    /// Current framebuffer width (updated on resize).
    [[nodiscard]] int  Width()  const noexcept { return m_width;  }

    /// Current framebuffer height (updated on resize).
    [[nodiscard]] int  Height() const noexcept { return m_height; }

    /// Returns the underlying GLFW window handle (needed by Vulkan surface creation).
    [[nodiscard]] GLFWwindow* NativeHandle() const noexcept { return m_window; }

    // -----------------------------------------------------------------------
    // Callbacks
    // -----------------------------------------------------------------------

    /**
     * @brief Registers a callback fired when the framebuffer is resized.
     *
     * Called with the new width and height in pixels.
     * Pass an empty std::function to unregister.
     */
    void SetResizeCallback(ResizeCallback cb);

    /**
     * @brief Registers a callback fired when the user requests window close.
     *
     * The default behaviour (close the window) is still applied; this
     * callback is an additional notification.
     */
    void SetCloseCallback(CloseCallback cb);

private:
    // -----------------------------------------------------------------------
    // Static GLFW C callbacks (forwarded to the instance via user pointer)
    // -----------------------------------------------------------------------
    static void GlfwErrorCallback(int error, const char* description);
    static void GlfwFramebufferSizeCallback(GLFWwindow* win, int w, int h);
    static void GlfwWindowCloseCallback(GLFWwindow* win);

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    GLFWwindow*    m_window  = nullptr; ///< GLFW window handle.
    int            m_width   = 0;       ///< Current framebuffer width in pixels.
    int            m_height  = 0;       ///< Current framebuffer height in pixels.
    ResizeCallback m_onResize;          ///< Registered resize callback.
    CloseCallback  m_onClose;           ///< Registered close callback.
};

} // namespace engine::platform
