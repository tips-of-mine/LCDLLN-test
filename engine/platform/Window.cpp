/**
 * @file Window.cpp
 * @brief GLFW window implementation — engine::platform::Window.
 *
 * GLFW is only included here (not in the header) to avoid leaking its
 * symbols into every translation unit.
 */

// GLFW: Vulkan is the rendering API; tell GLFW not to include any OpenGL
// headers.  The Vulkan surface is created externally (M01.1).
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "engine/platform/Window.h"
#include "engine/core/Log.h"

#include <string>
#include <cassert>

namespace engine::platform {

// ---------------------------------------------------------------------------
// Static GLFW error callback
// ---------------------------------------------------------------------------

/*static*/
void Window::GlfwErrorCallback(int error, const char* description) {
    LOG_ERROR(Platform, "GLFW error {}: {}", error, description ? description : "(null)");
}

// ---------------------------------------------------------------------------
// Static GLFW framebuffer-size callback
// ---------------------------------------------------------------------------

/*static*/
void Window::GlfwFramebufferSizeCallback(GLFWwindow* win, int w, int h) {
    // Retrieve the Window instance stored as the GLFW user pointer.
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (!self) { return; }

    self->m_width  = w;
    self->m_height = h;

    LOG_DEBUG(Platform, "Framebuffer resized → {}×{}", w, h);

    if (self->m_onResize) {
        self->m_onResize(w, h);
    }
}

// ---------------------------------------------------------------------------
// Static GLFW window-close callback
// ---------------------------------------------------------------------------

/*static*/
void Window::GlfwWindowCloseCallback(GLFWwindow* win) {
    auto* self = static_cast<Window*>(glfwGetWindowUserPointer(win));
    if (!self) { return; }

    LOG_DEBUG(Platform, "Window close requested");

    if (self->m_onClose) {
        self->m_onClose();
    }
    // glfwSetWindowShouldClose is set automatically by GLFW; we only notify.
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Window::Init(int width, int height, std::string_view title, bool fullscreen) {
    assert(m_window == nullptr && "Window::Init called twice");

    // Install error callback before glfwInit so early errors are caught.
    glfwSetErrorCallback(GlfwErrorCallback);

    if (!glfwInit()) {
        LOG_FATAL(Platform, "glfwInit() failed — cannot create window");
    }

    // We are targeting Vulkan; tell GLFW not to create an OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Allow window resize.
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    GLFWmonitor* monitor = nullptr;
    if (fullscreen) {
        monitor = glfwGetPrimaryMonitor();
        if (!monitor) {
            LOG_WARN(Platform, "Could not get primary monitor; falling back to windowed mode");
        }
    }

    const std::string titleStr{title};
    m_window = glfwCreateWindow(width, height, titleStr.c_str(), monitor, nullptr);
    if (!m_window) {
        glfwTerminate();
        LOG_FATAL(Platform, "glfwCreateWindow({}×{}) failed", width, height);
    }

    // Store this instance as the GLFW user pointer so static callbacks can
    // retrieve it.
    glfwSetWindowUserPointer(m_window, this);

    // Query actual framebuffer size (may differ from requested on HiDPI).
    glfwGetFramebufferSize(m_window, &m_width, &m_height);

    // Install GLFW callbacks.
    glfwSetFramebufferSizeCallback(m_window, GlfwFramebufferSizeCallback);
    glfwSetWindowCloseCallback    (m_window, GlfwWindowCloseCallback);

    LOG_INFO(Platform, "Window created — {}×{} \"{}\" (fullscreen={})",
             m_width, m_height, titleStr, fullscreen);
}

void Window::Shutdown() {
    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
        LOG_INFO(Platform, "Window destroyed");
    }
    glfwTerminate();
}

// ---------------------------------------------------------------------------
// Per-frame
// ---------------------------------------------------------------------------

void Window::PollEvents() {
    glfwPollEvents();
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

bool Window::ShouldClose() const noexcept {
    if (!m_window) { return true; }
    return glfwWindowShouldClose(m_window) != 0;
}

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

void Window::SetResizeCallback(ResizeCallback cb) {
    m_onResize = std::move(cb);
}

void Window::SetCloseCallback(CloseCallback cb) {
    m_onClose = std::move(cb);
}

} // namespace engine::platform
