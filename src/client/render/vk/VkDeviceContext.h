#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>

namespace engine::render
{
	/// Vulkan physical device, logical device, and graphics/present queues.
	/// GPU is selected by criteria: graphics + present + swapchain support; discrete GPU preferred.
	class VkDeviceContext final
	{
	public:
		static constexpr uint32_t kInvalidQueueFamily = UINT32_MAX;

		VkDeviceContext() = default;
		VkDeviceContext(const VkDeviceContext&) = delete;
		VkDeviceContext& operator=(const VkDeviceContext&) = delete;

		/// Creates the logical device and retrieves queues. Selects a physical device that supports
		/// graphics, present (for the given surface), and VK_KHR_swapchain. Prefers discrete GPU.
		/// \param instance Vulkan instance (must be valid).
		/// \param surface Presentation surface (must be valid for present queue selection).
		/// \return true on success.
		bool Create(::VkInstance instance, VkSurfaceKHR surface);

		/// Destroys the logical device. Safe to call multiple times. Does not destroy instance or surface.
		void Destroy();

		/// Returns the selected physical device (VK_NULL_HANDLE if not created).
		VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }

		/// Returns the logical device (VK_NULL_HANDLE if not created).
		::VkDevice GetDevice() const { return m_device; }

		/// Returns the graphics queue.
		VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }

		/// Returns the present queue.
		VkQueue GetPresentQueue() const { return m_presentQueue; }

		/// Returns the graphics queue family index, or kInvalidQueueFamily if not found.
		uint32_t GetGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }

		/// Returns the present queue family index, or kInvalidQueueFamily if not found.
		uint32_t GetPresentQueueFamilyIndex() const { return m_presentQueueFamilyIndex; }

		/// Returns true if Create() succeeded and the device is valid.
		bool IsValid() const { return m_device != VK_NULL_HANDLE; }

		/// Returns true if VK_KHR_synchronization2 (or Vulkan 1.3 core) is enabled; use vkCmdPipelineBarrier2 when true.
		bool SupportsSynchronization2() const { return m_sync2Supported; }
		/// Returns true if descriptor indexing features were enabled on the logical device.
		bool SupportsDescriptorIndexing() const { return m_descriptorIndexingSupported; }
		/// Returns true if dynamic rendering is enabled on the logical device.
		bool SupportsDynamicRendering() const { return m_dynamicRenderingSupported; }

		/// Non-null when SupportsDynamicRendering(); préfère KHR si les deux paires sont présentes.
		PFN_vkCmdBeginRendering GetCmdBeginRenderingCore() const { return m_pfnCmdBeginRendering; }
		PFN_vkCmdEndRendering GetCmdEndRenderingCore() const { return m_pfnCmdEndRendering; }
		PFN_vkCmdBeginRenderingKHR GetCmdBeginRenderingKHR() const { return m_pfnCmdBeginRenderingKHR; }
		PFN_vkCmdEndRenderingKHR GetCmdEndRenderingKHR() const { return m_pfnCmdEndRenderingKHR; }

	private:
		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		::VkDevice m_device = VK_NULL_HANDLE;
		VkQueue m_graphicsQueue = VK_NULL_HANDLE;
		VkQueue m_presentQueue = VK_NULL_HANDLE;
		uint32_t m_graphicsQueueFamilyIndex = kInvalidQueueFamily;
		uint32_t m_presentQueueFamilyIndex = kInvalidQueueFamily;
		bool m_sync2Supported = false;
		bool m_descriptorIndexingSupported = false;
		bool m_dynamicRenderingSupported = false;
		PFN_vkCmdBeginRendering m_pfnCmdBeginRendering = nullptr;
		PFN_vkCmdEndRendering m_pfnCmdEndRendering = nullptr;
		PFN_vkCmdBeginRenderingKHR m_pfnCmdBeginRenderingKHR = nullptr;
		PFN_vkCmdEndRenderingKHR m_pfnCmdEndRenderingKHR = nullptr;
	};
}
