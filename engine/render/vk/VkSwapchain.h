#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <vector>

namespace engine::render
{
	/// Swapchain, image views, render pass (1 color clear→present), and one framebuffer per image.
	/// Recreate on resize; extent is clamped to surface capabilities.
	class VkSwapchain final
	{
	public:
		VkSwapchain() = default;
		VkSwapchain(const VkSwapchain&) = delete;
		VkSwapchain& operator=(const VkSwapchain&) = delete;

		/// Creates swapchain, image views, render pass, and framebuffers.
		/// Format: SRGB if available. Present mode: use requestedPresentMode if supported, else FIFO.
		/// FIFO blocks at vkQueuePresent until next VBlank; MAILBOX is low-latency vsync; IMMEDIATE disables vsync.
		/// \param requestedPresentMode Preferred mode: VK_PRESENT_MODE_FIFO_KHR (vsync), VK_PRESENT_MODE_MAILBOX_KHR, or VK_PRESENT_MODE_IMMEDIATE_KHR.
		bool Create(VkPhysicalDevice physicalDevice, ::VkDevice device, VkSurfaceKHR surface,
			uint32_t graphicsQueueFamilyIndex, uint32_t presentQueueFamilyIndex,
			uint32_t requestedWidth, uint32_t requestedHeight,
			VkPresentModeKHR requestedPresentMode = VK_PRESENT_MODE_FIFO_KHR);

		/// Destroys framebuffers, render pass, image views, and swapchain. Safe to call multiple times.
		void Destroy();

		/// Destroys current swapchain/resources then creates again with new extent. Call after device idle.
		/// \return true on success.
		bool Recreate(uint32_t requestedWidth, uint32_t requestedHeight);

		/// Returns true when current surface caps imply a different swapchain extent.
		bool NeedsRecreateForSurfaceExtent(uint32_t requestedWidth, uint32_t requestedHeight) const;

		/// Returns the swapchain handle (VK_NULL_HANDLE if not created).
		VkSwapchainKHR GetSwapchain() const { return m_swapchain; }

		/// Returns the render pass (VK_NULL_HANDLE if not created).
		VkRenderPass GetRenderPass() const { return m_renderPass; }

		/// Returns the number of swapchain images (and framebuffers).
		uint32_t GetImageCount() const { return static_cast<uint32_t>(m_framebuffers.size()); }

		/// Returns the framebuffer for the given image index (0..GetImageCount()-1).
		VkFramebuffer GetFramebuffer(uint32_t imageIndex) const;

		/// Returns the swapchain image for the given image index (for Frame Graph / copy passes).
		VkImage GetImage(uint32_t imageIndex) const;

		/// Returns the image view for the given image index (for Frame Graph / copy passes).
		VkImageView GetImageView(uint32_t imageIndex) const;

		/// Returns the current swapchain image format.
		VkFormat GetImageFormat() const { return m_imageFormat; }

		/// Returns the current extent (width, height).
		VkExtent2D GetExtent() const { return m_extent; }

		/// Returns true if Create() succeeded and swapchain is valid.
		bool IsValid() const { return m_swapchain != VK_NULL_HANDLE; }

		/// Current present mode (stored for Recreate).
		VkPresentModeKHR GetPresentMode() const { return m_presentMode; }

	private:
		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		::VkDevice m_device = VK_NULL_HANDLE;
		VkSurfaceKHR m_surface = VK_NULL_HANDLE;
		uint32_t m_graphicsQueueFamilyIndex = 0;
		uint32_t m_presentQueueFamilyIndex = 0;
		VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
		VkPresentModeKHR m_requestedPresentMode = VK_PRESENT_MODE_FIFO_KHR;

		VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
		VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D m_extent{ 0, 0 };

		std::vector<VkImage> m_images;
		std::vector<VkImageView> m_imageViews;
		VkRenderPass m_renderPass = VK_NULL_HANDLE;
		std::vector<VkFramebuffer> m_framebuffers;
	};
}
