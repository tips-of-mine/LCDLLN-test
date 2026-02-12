#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace engine::platform {

class Window;

class Input {
public:
    enum class Key : std::uint8_t {
        W,
        A,
        S,
        D,
        Escape,
        F11,
        Count
    };

    bool Attach(Window& window);
    void Detach();

    void BeginFrame();
    void EndFrame();

    bool IsKeyDown(Key key) const;
    bool WasKeyPressed(Key key) const;

    float MouseDeltaX() const { return m_mouseDeltaX; }
    float MouseDeltaY() const { return m_mouseDeltaY; }

    void SetCursorCaptured(bool captured);
    bool IsCursorCaptured() const { return m_cursorCaptured; }

private:
    static int ToGlfwKey(Key key);

    Window* m_window = nullptr;
    std::array<bool, static_cast<std::size_t>(Key::Count)> m_curr{};
    std::array<bool, static_cast<std::size_t>(Key::Count)> m_prev{};

    double m_lastMouseX = 0.0;
    double m_lastMouseY = 0.0;
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    bool m_firstMouseSample = true;
    bool m_cursorCaptured = false;
};

} // namespace engine::platform
