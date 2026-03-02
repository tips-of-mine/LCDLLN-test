/**
 * @file Input.cpp
 * @brief Input implementation using GLFW. Key states, mouse delta, cursor capture.
 */

#include "engine/platform/Input.h"
#include "engine/platform/Window.h"
#include <GLFW/glfw3.h>
#include <cstring>

namespace engine::platform {

void Input::Update(Window* window) {
    void* handle = window ? window->GetNativeHandle() : nullptr;
    GLFWwindow* win = static_cast<GLFWwindow*>(handle);
    if (!win) {
        m_mouseDeltaX = 0.0;
        m_mouseDeltaY = 0.0;
        return;
    }
    for (int i = 0; i < kKeyStateSize; ++i)
        m_keyState[i] = (glfwGetKey(win, i) == GLFW_PRESS);
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(win, &x, &y);
    if (m_firstMouse) {
        m_lastMouseX = x;
        m_lastMouseY = y;
        m_firstMouse = false;
    }
    m_mouseDeltaX = x - m_lastMouseX;
    m_mouseDeltaY = y - m_lastMouseY;
    m_lastMouseX = x;
    m_lastMouseY = y;
}

bool Input::GetKey(int keyCode) const {
    if (keyCode < 0 || keyCode >= kKeyStateSize) return false;
    return m_keyState[keyCode];
}

void Input::GetMouseDelta(double* outDeltaX, double* outDeltaY) const {
    if (outDeltaX) *outDeltaX = m_mouseDeltaX;
    if (outDeltaY) *outDeltaY = m_mouseDeltaY;
}

void Input::SetCursorCaptured(Window* window, bool captured) {
    void* handle = window ? window->GetNativeHandle() : nullptr;
    GLFWwindow* win = static_cast<GLFWwindow*>(handle);
    if (!win) return;
    m_cursorCaptured = captured;
    if (captured) {
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
        glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

} // namespace engine::platform
