#pragma once

#include <cstdint>
#include <string>

struct GLFWwindow;

namespace engine::platform {

struct WindowDesc {
    std::string title = "LCDLLN";
    std::int32_t width = 1280;
    std::int32_t height = 720;
    bool fullscreen = false;
};

class Window {
public:
    Window();
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(const WindowDesc& desc);
    void Destroy();

    void PollEvents();
    bool ShouldClose() const;
    void RequestClose();

    void ToggleFullscreen();

    std::int32_t Width() const { return m_width; }
    std::int32_t Height() const { return m_height; }
    bool WasResized() const { return m_wasResized; }
    bool IsFullscreen() const { return m_fullscreen; }

    void ResetFrameFlags();

    GLFWwindow* NativeHandle() const { return m_window; }

private:
    static void ResizeCallback(GLFWwindow* window, int width, int height);
    static void CloseCallback(GLFWwindow* window);

    void OnResize(int width, int height);
    void OnClose();

    GLFWwindow* m_window = nullptr;
    std::string m_title;
    std::int32_t m_width = 0;
    std::int32_t m_height = 0;
    bool m_fullscreen = false;
    bool m_wasResized = false;
    bool m_closeRequested = false;

    std::int32_t m_windowedX = 100;
    std::int32_t m_windowedY = 100;
    std::int32_t m_windowedWidth = 1280;
    std::int32_t m_windowedHeight = 720;
};

} // namespace engine::platform
