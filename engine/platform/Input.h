#pragma once

/**
 * @file Input.h
 * @brief Keyboard and mouse input abstraction for the engine platform layer.
 *
 * Input works in a two-step model:
 *   1. Install the GLFW callbacks via Input::Install(window).
 *   2. Call Input::BeginFrame() once per game-loop iteration, before reading
 *      any input state.  This snaps the "current" state, records mouse delta,
 *      etc.
 *
 * Typical usage:
 *   Input::Install(window.NativeHandle());
 *   while (!window.ShouldClose()) {
 *       window.PollEvents();        // drives GLFW callbacks
 *       Input::BeginFrame();        // finalise per-frame state
 *
 *       if (Input::IsKeyDown(Key::W)) { ... }
 *       float dx = Input::MouseDeltaX();
 *   }
 *   Input::Uninstall();
 */

#include <cstdint>

struct GLFWwindow;

namespace engine::platform {

// ---------------------------------------------------------------------------
// Key enum — commonly used keys (WASD + extras)
// ---------------------------------------------------------------------------

/**
 * @brief Symbolic key identifiers.
 *
 * Values mirror GLFW_KEY_* constants so they can be cast directly; avoid
 * relying on that equivalence in application code.
 */
enum class Key : int {
    // Alphabet
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    // Digits (top row)
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,

    // Function keys
    F1  = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Navigation / misc
    Escape    = 256,
    Enter     = 257,
    Tab       = 258,
    Backspace = 259,
    Space     = 32,
    Left      = 263,
    Right     = 262,
    Up        = 265,
    Down      = 264,
    LeftShift = 340,
    LeftCtrl  = 341,
    LeftAlt   = 342,
    LeftSuper = 343,
};

// ---------------------------------------------------------------------------
// Mouse button enum
// ---------------------------------------------------------------------------

/**
 * @brief Symbolic mouse button identifiers.
 * Values mirror GLFW_MOUSE_BUTTON_*.
 */
enum class MouseButton : int {
    Left   = 0,
    Right  = 1,
    Middle = 2,
};

// ---------------------------------------------------------------------------
// Input subsystem (static interface)
// ---------------------------------------------------------------------------

/**
 * @brief Static input subsystem: keyboard + mouse state, per-frame deltas.
 *
 * All public methods are thread-hostile: call them only from the main thread
 * (same thread as glfwPollEvents).
 */
class Input {
public:
    Input() = delete;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /**
     * @brief Installs GLFW callbacks on the given window.
     *
     * Must be called after the GLFW window is created and before the first
     * frame.
     *
     * @param window  GLFW window handle returned by Window::NativeHandle().
     */
    static void Install(GLFWwindow* window);

    /**
     * @brief Removes GLFW callbacks and resets all internal state.
     *
     * Safe to call even if Install() was never called.
     */
    static void Uninstall();

    // -----------------------------------------------------------------------
    // Per-frame update
    // -----------------------------------------------------------------------

    /**
     * @brief Snapshots input state for the current frame.
     *
     * Call this once per frame, after glfwPollEvents() (i.e. after
     * Window::PollEvents()), before reading any key/mouse state.
     *
     * - Moves "current" key/button state to "previous".
     * - Computes mouse delta from raw position accumulation.
     * - Resets per-frame accumulators.
     */
    static void BeginFrame();

    // -----------------------------------------------------------------------
    // Keyboard state
    // -----------------------------------------------------------------------

    /**
     * @brief Returns true while the key is held down this frame.
     * @param key  Key to query.
     */
    [[nodiscard]] static bool IsKeyDown(Key key) noexcept;

    /**
     * @brief Returns true on the first frame the key was pressed.
     * @param key  Key to query.
     */
    [[nodiscard]] static bool IsKeyPressed(Key key) noexcept;

    /**
     * @brief Returns true on the first frame the key was released.
     * @param key  Key to query.
     */
    [[nodiscard]] static bool IsKeyReleased(Key key) noexcept;

    // -----------------------------------------------------------------------
    // Mouse buttons
    // -----------------------------------------------------------------------

    /// Returns true while the mouse button is held down.
    [[nodiscard]] static bool IsMouseButtonDown(MouseButton btn) noexcept;

    /// Returns true on the first frame the mouse button was pressed.
    [[nodiscard]] static bool IsMouseButtonPressed(MouseButton btn) noexcept;

    // -----------------------------------------------------------------------
    // Mouse position & delta
    // -----------------------------------------------------------------------

    /// Absolute cursor X position in screen coordinates (pixels from left).
    [[nodiscard]] static float MouseX() noexcept;

    /// Absolute cursor Y position in screen coordinates (pixels from top).
    [[nodiscard]] static float MouseY() noexcept;

    /**
     * @brief Cursor displacement since the previous frame on the X axis.
     *
     * When cursor capture is enabled (CaptureCursor), GLFW uses raw mouse
     * motion so values are unbounded.  Without capture, delta is computed
     * from screen-space position differences.
     */
    [[nodiscard]] static float MouseDeltaX() noexcept;

    /// Cursor displacement since the previous frame on the Y axis.
    [[nodiscard]] static float MouseDeltaY() noexcept;

    // -----------------------------------------------------------------------
    // Cursor capture
    // -----------------------------------------------------------------------

    /**
     * @brief Hides and locks the cursor to the window for mouse-look.
     *
     * Uses GLFW_CURSOR_DISABLED which also enables raw motion input when
     * the platform supports it (eliminates acceleration).
     */
    static void CaptureCursor();

    /**
     * @brief Releases the cursor back to normal OS operation.
     */
    static void ReleaseCursor();

    /// Returns true if the cursor is currently captured.
    [[nodiscard]] static bool IsCursorCaptured() noexcept;

private:
    // -----------------------------------------------------------------------
    // GLFW C callbacks
    // -----------------------------------------------------------------------
    static void GlfwKeyCallback      (GLFWwindow*, int key, int scancode, int action, int mods);
    static void GlfwMouseButtonCallback(GLFWwindow*, int button, int action, int mods);
    static void GlfwCursorPosCallback (GLFWwindow*, double xpos, double ypos);
};

} // namespace engine::platform
