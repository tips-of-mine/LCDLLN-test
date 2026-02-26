/**
 * @file Input.cpp
 * @brief Keyboard and mouse input implementation — engine::platform::Input.
 *
 * State model:
 *   g_keyCurrent[]  — key down/up as reported by GLFW callbacks (written
 *                     asynchronously within glfwPollEvents).
 *   g_keyPrev[]     — snapshot of g_keyCurrent taken at BeginFrame().
 *
 *   IsKeyDown()     = g_keyCurrent[key]
 *   IsKeyPressed()  = g_keyCurrent[key] && !g_keyPrev[key]
 *   IsKeyReleased() = !g_keyCurrent[key] && g_keyPrev[key]
 *
 * Mouse delta is computed from the difference between the cursor position
 * polled this frame and the position polled last frame.  With cursor capture
 * GLFW provides raw motion, which is also accumulated across the frame here.
 */

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "engine/platform/Input.h"
#include "engine/core/Log.h"

#include <array>
#include <cstring>

namespace engine::platform {

// ---------------------------------------------------------------------------
// Internal state  (anonymous namespace → translation-unit scope)
// ---------------------------------------------------------------------------
namespace {

/// Maximum GLFW key code tracked.  GLFW_KEY_LAST == 348.
constexpr int kMaxKeys    = 512;
/// Maximum GLFW mouse button index.  GLFW_MOUSE_BUTTON_LAST == 7.
constexpr int kMaxButtons = 8;

/// Current-frame key state (true = pressed).
std::array<bool, kMaxKeys>    g_keyCurrent  {};
/// Previous-frame key state.
std::array<bool, kMaxKeys>    g_keyPrev     {};

/// Current-frame mouse button state.
std::array<bool, kMaxButtons> g_btnCurrent  {};
/// Previous-frame mouse button state.
std::array<bool, kMaxButtons> g_btnPrev     {};

/// Absolute cursor position (screen coords) as of the last raw-motion event.
double g_cursorX     = 0.0;
double g_cursorY     = 0.0;
/// Cursor position at the start of the previous frame (for delta computation).
double g_prevCursorX = 0.0;
double g_prevCursorY = 0.0;

/// Per-frame computed delta.
float g_deltaX = 0.0f;
float g_deltaY = 0.0f;

bool         g_cursorCaptured = false;
GLFWwindow*  g_window         = nullptr;

} // anonymous namespace

// ---------------------------------------------------------------------------
// GLFW C callbacks
// ---------------------------------------------------------------------------

/*static*/
void Input::GlfwKeyCallback(GLFWwindow* /*win*/, int key,
                             int /*scancode*/, int action, int /*mods*/) {
    if (key < 0 || key >= kMaxKeys) { return; }
    if      (action == GLFW_PRESS)   { g_keyCurrent[key] = true;  }
    else if (action == GLFW_RELEASE) { g_keyCurrent[key] = false; }
    // GLFW_REPEAT is intentionally ignored: IsKeyDown() already covers held keys.
}

/*static*/
void Input::GlfwMouseButtonCallback(GLFWwindow* /*win*/, int button,
                                    int action, int /*mods*/) {
    if (button < 0 || button >= kMaxButtons) { return; }
    if      (action == GLFW_PRESS)   { g_btnCurrent[button] = true;  }
    else if (action == GLFW_RELEASE) { g_btnCurrent[button] = false; }
}

/*static*/
void Input::GlfwCursorPosCallback(GLFWwindow* /*win*/, double xpos, double ypos) {
    g_cursorX = xpos;
    g_cursorY = ypos;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

/*static*/
void Input::Install(GLFWwindow* window) {
    g_window = window;

    g_keyCurrent.fill(false);
    g_keyPrev.fill(false);
    g_btnCurrent.fill(false);
    g_btnPrev.fill(false);
    g_cursorX = g_cursorY = g_prevCursorX = g_prevCursorY = 0.0;
    g_deltaX  = g_deltaY  = 0.0f;
    g_cursorCaptured      = false;

    // Register GLFW callbacks.
    glfwSetKeyCallback        (window, GlfwKeyCallback);
    glfwSetMouseButtonCallback(window, GlfwMouseButtonCallback);
    glfwSetCursorPosCallback  (window, GlfwCursorPosCallback);

    // Seed initial cursor position so the first BeginFrame() delta is zero.
    glfwGetCursorPos(window, &g_cursorX, &g_cursorY);
    g_prevCursorX = g_cursorX;
    g_prevCursorY = g_cursorY;

    LOG_INFO(Platform, "Input subsystem installed");
}

/*static*/
void Input::Uninstall() {
    if (g_window) {
        // Unregister callbacks.
        glfwSetKeyCallback        (g_window, nullptr);
        glfwSetMouseButtonCallback(g_window, nullptr);
        glfwSetCursorPosCallback  (g_window, nullptr);
        g_window = nullptr;
    }
    g_keyCurrent.fill(false);
    g_keyPrev.fill(false);
    g_btnCurrent.fill(false);
    g_btnPrev.fill(false);
    g_cursorCaptured = false;
    LOG_INFO(Platform, "Input subsystem uninstalled");
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

/*static*/
void Input::BeginFrame() {
    // Snap key state.
    g_keyPrev = g_keyCurrent;
    g_btnPrev = g_btnCurrent;

    // Compute mouse delta from the raw cursor position.
    g_deltaX = static_cast<float>(g_cursorX - g_prevCursorX);
    g_deltaY = static_cast<float>(g_cursorY - g_prevCursorY);

    g_prevCursorX = g_cursorX;
    g_prevCursorY = g_cursorY;
}

// ---------------------------------------------------------------------------
// Keyboard state
// ---------------------------------------------------------------------------

/*static*/
bool Input::IsKeyDown(Key key) noexcept {
    const int k = static_cast<int>(key);
    if (k < 0 || k >= kMaxKeys) { return false; }
    return g_keyCurrent[k];
}

/*static*/
bool Input::IsKeyPressed(Key key) noexcept {
    const int k = static_cast<int>(key);
    if (k < 0 || k >= kMaxKeys) { return false; }
    return g_keyCurrent[k] && !g_keyPrev[k];
}

/*static*/
bool Input::IsKeyReleased(Key key) noexcept {
    const int k = static_cast<int>(key);
    if (k < 0 || k >= kMaxKeys) { return false; }
    return !g_keyCurrent[k] && g_keyPrev[k];
}

// ---------------------------------------------------------------------------
// Mouse buttons
// ---------------------------------------------------------------------------

/*static*/
bool Input::IsMouseButtonDown(MouseButton btn) noexcept {
    const int b = static_cast<int>(btn);
    if (b < 0 || b >= kMaxButtons) { return false; }
    return g_btnCurrent[b];
}

/*static*/
bool Input::IsMouseButtonPressed(MouseButton btn) noexcept {
    const int b = static_cast<int>(btn);
    if (b < 0 || b >= kMaxButtons) { return false; }
    return g_btnCurrent[b] && !g_btnPrev[b];
}

// ---------------------------------------------------------------------------
// Mouse position & delta
// ---------------------------------------------------------------------------

/*static*/
float Input::MouseX() noexcept {
    return static_cast<float>(g_cursorX);
}

/*static*/
float Input::MouseY() noexcept {
    return static_cast<float>(g_cursorY);
}

/*static*/
float Input::MouseDeltaX() noexcept {
    return g_deltaX;
}

/*static*/
float Input::MouseDeltaY() noexcept {
    return g_deltaY;
}

// ---------------------------------------------------------------------------
// Cursor capture
// ---------------------------------------------------------------------------

/*static*/
void Input::CaptureCursor() {
    if (!g_window || g_cursorCaptured) { return; }

    glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // Enable raw mouse motion if the platform supports it (removes acceleration).
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    // Reset delta accumulation so the first captured frame has zero delta.
    glfwGetCursorPos(g_window, &g_cursorX, &g_cursorY);
    g_prevCursorX = g_cursorX;
    g_prevCursorY = g_cursorY;
    g_deltaX = g_deltaY = 0.0f;

    g_cursorCaptured = true;
    LOG_DEBUG(Platform, "Cursor captured (raw motion={})",
              glfwRawMouseMotionSupported() ? "on" : "off");
}

/*static*/
void Input::ReleaseCursor() {
    if (!g_window || !g_cursorCaptured) { return; }

    glfwSetInputMode(g_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(g_window, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    }
    g_cursorCaptured = false;
    LOG_DEBUG(Platform, "Cursor released");
}

/*static*/
bool Input::IsCursorCaptured() noexcept {
    return g_cursorCaptured;
}

} // namespace engine::platform
