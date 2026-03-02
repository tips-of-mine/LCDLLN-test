/**
 * @file Input.h
 * @brief Input abstraction: key states (WASD), mouse delta, cursor capture/release.
 */

#pragma once

namespace engine::platform {

class Window;

/**
 * Input state: updated per frame. Key states, mouse delta, cursor capture.
 * Must be updated each frame (Update) with the active window.
 */
class Input {
public:
    Input() = default;

    /** Update key states and mouse delta for this frame. Call once per frame with the window. */
    void Update(Window* window);

    /** Key state: true if currently held this frame. */
    bool GetKey(int keyCode) const;
    bool GetKeyW() const { return GetKey(0x57); }
    bool GetKeyA() const { return GetKey(0x41); }
    bool GetKeyS() const { return GetKey(0x53); }
    bool GetKeyD() const { return GetKey(0x44); }

    /** Mouse delta since last Update (x, y). */
    void GetMouseDelta(double* outDeltaX, double* outDeltaY) const;
    double GetMouseDeltaX() const { return m_mouseDeltaX; }
    double GetMouseDeltaY() const { return m_mouseDeltaY; }

    /** Capture cursor (hide + confine to window for mouse look). */
    void SetCursorCaptured(Window* window, bool captured);

    /** True if cursor is currently captured. */
    bool IsCursorCaptured() const { return m_cursorCaptured; }

private:
    static constexpr int kKeyStateSize = 512;
    bool m_keyState[kKeyStateSize] = {};
    double m_mouseDeltaX = 0.0;
    double m_mouseDeltaY = 0.0;
    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    bool m_firstMouse = true;
    bool m_cursorCaptured = false;
};

} // namespace engine::platform
