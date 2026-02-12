#include "engine/platform/Input.h"

#include "engine/platform/Window.h"

#if ENGINE_HAS_GLFW
#include <GLFW/glfw3.h>
#endif

namespace engine::platform {

bool Input::Attach(Window& window) {
    m_window = &window;
#if ENGINE_HAS_GLFW
    if (m_window->NativeHandle() == nullptr) {
        return false;
    }

    glfwGetCursorPos(m_window->NativeHandle(), &m_lastMouseX, &m_lastMouseY);
    m_firstMouseSample = true;
    return true;
#else
    return false;
#endif
}

void Input::Detach() {
    m_window = nullptr;
}

void Input::BeginFrame() {
    m_prev = m_curr;
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;

#if ENGINE_HAS_GLFW
    if (m_window == nullptr || m_window->NativeHandle() == nullptr) {
        return;
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(Key::Count); ++i) {
        const int glfwKey = ToGlfwKey(static_cast<Key>(i));
        m_curr[i] = glfwGetKey(m_window->NativeHandle(), glfwKey) == GLFW_PRESS;
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(m_window->NativeHandle(), &mouseX, &mouseY);

    if (m_firstMouseSample) {
        m_lastMouseX = mouseX;
        m_lastMouseY = mouseY;
        m_firstMouseSample = false;
    }

    m_mouseDeltaX = static_cast<float>(mouseX - m_lastMouseX);
    m_mouseDeltaY = static_cast<float>(mouseY - m_lastMouseY);
    m_lastMouseX = mouseX;
    m_lastMouseY = mouseY;
#endif
}

void Input::EndFrame() {}

bool Input::IsKeyDown(Key key) const {
    return m_curr[static_cast<std::size_t>(key)];
}

bool Input::WasKeyPressed(Key key) const {
    const std::size_t i = static_cast<std::size_t>(key);
    return m_curr[i] && !m_prev[i];
}

void Input::SetCursorCaptured(bool captured) {
    m_cursorCaptured = captured;
#if ENGINE_HAS_GLFW
    if (m_window == nullptr || m_window->NativeHandle() == nullptr) {
        return;
    }

    glfwSetInputMode(m_window->NativeHandle(), GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
#endif
}

int Input::ToGlfwKey(Key key) {
#if ENGINE_HAS_GLFW
    switch (key) {
        case Key::W: return GLFW_KEY_W;
        case Key::A: return GLFW_KEY_A;
        case Key::S: return GLFW_KEY_S;
        case Key::D: return GLFW_KEY_D;
        case Key::Escape: return GLFW_KEY_ESCAPE;
        case Key::F11: return GLFW_KEY_F11;
        case Key::Count: break;
    }
#else
    (void)key;
#endif
    return 0;
}

} // namespace engine::platform
