#include "engine/platform/Window.h"

#include "engine/core/Log.h"

#if ENGINE_HAS_GLFW
#include <GLFW/glfw3.h>
#endif

namespace engine::platform {

namespace {

#if ENGINE_HAS_GLFW
int g_glfwRefCount = 0;
#endif

} // namespace

Window::Window() = default;

Window::~Window() {
    Destroy();
}

bool Window::Create(const WindowDesc& desc) {
#if ENGINE_HAS_GLFW
    if (m_window != nullptr) {
        return true;
    }

    if (g_glfwRefCount == 0 && glfwInit() == GLFW_FALSE) {
        LOG_ERROR(Core, "GLFW initialization failed");
        return false;
    }
    ++g_glfwRefCount;

    m_title = desc.title;
    m_width = desc.width;
    m_height = desc.height;
    m_fullscreen = desc.fullscreen;

    GLFWmonitor* monitor = nullptr;
    if (desc.fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        if (monitor != nullptr) {
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            if (mode != nullptr) {
                m_width = mode->width;
                m_height = mode->height;
            }
        }
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), monitor, nullptr);
    if (m_window == nullptr) {
        LOG_ERROR(Core, "Window creation failed");
        Destroy();
        return false;
    }

    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, &Window::ResizeCallback);
    glfwSetWindowCloseCallback(m_window, &Window::CloseCallback);

    return true;
#else
    (void)desc;
    LOG_ERROR(Core, "GLFW headers not found: window system unavailable");
    return false;
#endif
}

void Window::Destroy() {
#if ENGINE_HAS_GLFW
    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    if (g_glfwRefCount > 0) {
        --g_glfwRefCount;
        if (g_glfwRefCount == 0) {
            glfwTerminate();
        }
    }
#endif

    m_closeRequested = true;
}

void Window::PollEvents() {
#if ENGINE_HAS_GLFW
    glfwPollEvents();
#endif
}

bool Window::ShouldClose() const {
#if ENGINE_HAS_GLFW
    return m_closeRequested || (m_window != nullptr && glfwWindowShouldClose(m_window) != 0);
#else
    return m_closeRequested;
#endif
}

void Window::RequestClose() {
    m_closeRequested = true;
#if ENGINE_HAS_GLFW
    if (m_window != nullptr) {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }
#endif
}

void Window::ToggleFullscreen() {
#if ENGINE_HAS_GLFW
    if (m_window == nullptr) {
        return;
    }

    m_fullscreen = !m_fullscreen;
    if (m_fullscreen) {
        glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
        glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);

        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        if (monitor == nullptr) {
            m_fullscreen = false;
            return;
        }

        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode == nullptr) {
            m_fullscreen = false;
            return;
        }

        glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        glfwSetWindowMonitor(m_window, nullptr, m_windowedX, m_windowedY, m_windowedWidth, m_windowedHeight, 0);
    }
#else
    m_fullscreen = false;
#endif
}

void Window::ResetFrameFlags() {
    m_wasResized = false;
}

void Window::ResizeCallback(GLFWwindow* window, int width, int height) {
#if ENGINE_HAS_GLFW
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr) {
        self->OnResize(width, height);
    }
#else
    (void)window;
    (void)width;
    (void)height;
#endif
}

void Window::CloseCallback(GLFWwindow* window) {
#if ENGINE_HAS_GLFW
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self != nullptr) {
        self->OnClose();
    }
#else
    (void)window;
#endif
}

void Window::OnResize(int width, int height) {
    m_width = width;
    m_height = height;
    m_wasResized = true;
}

void Window::OnClose() {
    m_closeRequested = true;
}

} // namespace engine::platform
