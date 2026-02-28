#pragma once

/**
 * @file VkInstance.h
 * @brief Vulkan instance + debug messenger + surface wrapper.
 *
 * Ticket: M01.1 — Vulkan: instance, validation, surface.
 *
 * Responsibilities:
 *   - Create and own a VkInstance configured with the required extensions.
 *   - Enable validation layers in debug builds only.
 *   - Install a VK_EXT_debug_utils messenger and route messages to the Log
 *     subsystem (Render channel).
 *   - Create a VkSurfaceKHR for the engine main window via GLFW.
 *   - Destroy all Vulkan objects in the correct order on shutdown.
 */

#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace engine::render::vk {

/**
 * @brief RAII-style wrapper around a Vulkan instance, debug messenger and surface.
 *
 * The typical usage pattern is:
 *   - Construct an Instance object.
 *   - Call Init(window) once after the GLFW window exists.
 *   - Use Get() / Surface() to access the underlying Vulkan handles.
 *   - Call Shutdown() before application exit (or rely on the destructor).
 *
 * Init() is safe to call once; repeated calls are ignored and return true.
 * All public methods are expected to be called from the main (render) thread.
 */
class Instance {
public:
    /// Default constructor creates an empty, invalid instance wrapper.
    Instance() = default;

    /// Destructor automatically calls Shutdown() if needed.
    ~Instance();

    // Non-copyable, non-movable.
    Instance(const Instance&)            = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&&)                 = delete;
    Instance& operator=(Instance&&)      = delete;

    /**
     * @brief Initialises Vulkan (instance + optional debug messenger + surface).
     *
     * In debug builds, the KHRONOS validation layer is enabled when available
     * and a debug messenger is installed via VK_EXT_debug_utils.  In release
     * builds, no validation layers or debug messenger are created.
     *
     * @param window  GLFW window handle used to create the VkSurfaceKHR.
     * @return        true on success, false on unrecoverable error.
     *
     * On failure, an error is logged via LOG_ERROR(Render, ...) and the
     * internal handles remain invalid; Shutdown() is still safe to call.
     */
    [[nodiscard]] bool Init(GLFWwindow* window);

    /**
     * @brief Destroys the surface, debug messenger and instance in order.
     *
     * The destruction order is:
     *   1) VkSurfaceKHR
     *   2) VkDebugUtilsMessengerEXT
     *   3) VkInstance
     *
     * The method is idempotent and may be called multiple times.
     */
    void Shutdown();

    /**
     * @brief Returns true if the instance handle is valid.
     */
    [[nodiscard]] bool IsValid() const noexcept { return m_instance != VK_NULL_HANDLE; }

    /**
     * @brief Returns the underlying VkInstance handle.
     */
    [[nodiscard]] VkInstance Get() const noexcept { return m_instance; }

    /**
     * @brief Returns the window surface created for this instance.
     */
    [[nodiscard]] VkSurfaceKHR Surface() const noexcept { return m_surface; }

private:
    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface        = VK_NULL_HANDLE;
};

} // namespace engine::render::vk

