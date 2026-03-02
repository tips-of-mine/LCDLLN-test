/**
 * @file Window.cpp
 * @brief GLFW-based window implementation. Resize and close callbacks.
 */

#include "engine/platform/Window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace engine::platform {

namespace {
Window* GetWindow(GLFWwindow* win) {
    return static_cast<Window*>(glfwGetWindowUserPointer(win));
}
} // namespace

static void OnClose(GLFWwindow* win) {
    Window* self = GetWindow(win);
    if (self && self->m_onClose) self->m_onClose();
}

Window::~Window() {
    Destroy();
}

bool Window::Create(const std::string& title, int width, int height) {
    if (m_window) return true;
    static bool glfwInited = false;
    if (!glfwInited) {
        if (!glfwInit()) return false;
        glfwInited = true;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* win = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!win) return false;
    m_window = win;
    glfwSetWindowUserPointer(win, this);
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow* w, int a, int b) {
        Window* self = GetWindow(w);
        if (self) { self->m_width = a; self->m_height = b; if (self->m_onResize) self->m_onResize(a, b); }
    });
    glfwSetWindowCloseCallback(win, OnClose);
    m_width = width;
    m_height = height;
    int w = 0, h = 0;
    glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_window), &w, &h);
    if (w > 0 && h > 0) { m_width = w; m_height = h; }
    return true;
}

void Window::Destroy() {
    if (m_window) {
        GLFWwindow* win = static_cast<GLFWwindow*>(m_window);
        glfwSetFramebufferSizeCallback(win, nullptr);
        glfwSetWindowCloseCallback(win, nullptr);
        glfwSetWindowUserPointer(win, nullptr);
        glfwDestroyWindow(win);
        m_window = nullptr;
    }
    m_width = 0;
    m_height = 0;
}

void Window::PollEvents() {
    glfwPollEvents();
}

bool Window::ShouldClose() const {
    return m_window ? (glfwWindowShouldClose(static_cast<GLFWwindow*>(m_window)) != 0) : true;
}

void* Window::GetNativeHandle() const {
    return m_window;
}

} // namespace engine::platform
