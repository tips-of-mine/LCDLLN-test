#pragma once

#include <vulkan/vulkan_core.h>

struct GLFWwindow;

namespace engine::render
{
	/// Vulkan instance with optional debug utils and surface (GLFW).
	/// Validation layers and debug messenger are enabled only in debug builds.
	class VkInstance final
	{
	public:
		VkInstance() = default;
		VkInstance(const VkInstance&) = delete;
		VkInstance& operator=(const VkInstance&) = delete;

		/// Creates the Vulkan instance with required extensions and optional validation layers (debug).
		/// \return true on success.
		bool Create();

		/// Creates the presentation surface from a GLFW window.
		/// Must be called after Create(). Safe to call with nullptr (no surface created).
		/// \return true on success or if window is nullptr.
		bool CreateSurface(GLFWwindow* window);

		/// Destroys surface, debug messenger (if any), then instance. Safe to call multiple times.
		void Destroy();

		/// Returns the Vulkan instance handle (VK_NULL_HANDLE if not created).
		::VkInstance GetHandle() const { return m_instance; }

		/// Returns the Vulkan instance handle for API calls (alias for GetHandle()).
		::VkInstance Get() const { return m_instance; }

		/// Returns the surface handle (VK_NULL_HANDLE if not created).
		VkSurfaceKHR GetSurface() const { return m_surface; }

		/// Returns true if the instance (and optionally surface) were created successfully.
		bool IsValid() const { return m_instance != VK_NULL_HANDLE; }

	private:
		::VkInstance m_instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_surface = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
	};
}
