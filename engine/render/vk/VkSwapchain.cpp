#include "engine/render/vk/VkSwapchain.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace engine::render
{
	namespace
	{
		VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats)
		{
			for (const auto& f : formats)
			{
				if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				{
					return f;
				}
			}
			return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
		}

		VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes, VkPresentModeKHR requested)
		{
			for (VkPresentModeKHR m : modes)
			{
				if (m == requested)
					return m;
			}
			// FIFO is required by the spec to be always available; fallback for compatibility.
			for (VkPresentModeKHR m : modes)
			{
				if (m == VK_PRESENT_MODE_FIFO_KHR)
					return m;
			}
			return modes.empty() ? VK_PRESENT_MODE_FIFO_KHR : modes[0];
		}

		VkExtent2D ClampExtent(uint32_t requestedWidth, uint32_t requestedHeight,
			const VkSurfaceCapabilitiesKHR& caps)
		{
			// Sur certaines plateformes/états de fenêtre, caps.currentExtent peut rapporter une valeur
			// incohérente (ex: height=1) alors que l'extent demandée correspond à la taille réelle.
			// On ne l'utilise que si elle correspond vraiment aux dimensions demandées.
			//
			// Cas très dégénéré observé côté user: min=max=currentExtent et height=1
			// -> dans ce cas, le clamp force l'extent à 120x1, ce qui casse tout le resize.
			const bool currentFixed =
				caps.currentExtent.width != UINT32_MAX && caps.currentExtent.height != UINT32_MAX;
			const bool capsDegenerateHeight1 =
				caps.minImageExtent.height <= 1 &&
				caps.maxImageExtent.height <= 1 &&
				caps.minImageExtent.height == caps.maxImageExtent.height &&
				requestedHeight > 1;

			if (capsDegenerateHeight1)
			{
				LOG_WARN(Render, "[SWAPCHAIN] Degenerate caps detected -> bypass clamp requested={}x{} caps min={}x{} max={}x{} current={}x{}", requestedWidth, requestedHeight, caps.minImageExtent.width, caps.minImageExtent.height, caps.maxImageExtent.width, caps.maxImageExtent.height, caps.currentExtent.width, caps.currentExtent.height);
				return VkExtent2D{ requestedWidth, requestedHeight };
			}

			if (currentFixed &&
				caps.currentExtent.width == requestedWidth &&
				caps.currentExtent.height == requestedHeight)
			{
				return caps.currentExtent;
			}
			VkExtent2D extent{};
			extent.width = std::clamp(requestedWidth,
				caps.minImageExtent.width, caps.maxImageExtent.width);
			extent.height = std::clamp(requestedHeight,
				caps.minImageExtent.height, caps.maxImageExtent.height);
			return extent;
		}
	}

	bool VkSwapchain::Create(VkPhysicalDevice physicalDevice, ::VkDevice device, VkSurfaceKHR surface,
		uint32_t graphicsQueueFamilyIndex, uint32_t presentQueueFamilyIndex,
		uint32_t requestedWidth, uint32_t requestedHeight,
		VkPresentModeKHR requestedPresentMode)
	{
		LOG_DEBUG(Render, "[SWAPCHAIN] Create enter w={} h={}", requestedWidth, requestedHeight);
		if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "VkSwapchain::Create: invalid physical device, device, or surface");
			return false;
		}

		VkSurfaceCapabilitiesKHR caps{};
		VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed: {}", static_cast<int>(result));
			return false;
		}

		// Debug: on veut comprendre quand l'extent "currentExtent" des caps est incohérente.
		if (caps.currentExtent.height == 1 && requestedHeight > 1)
		{
				LOG_DEBUG(Render, "[SWAPCHAIN] caps currentExtent={}x{} min={}x{} max={}x{} requested={}x{}", caps.currentExtent.width, caps.currentExtent.height, caps.minImageExtent.width, caps.minImageExtent.height, caps.maxImageExtent.width, caps.maxImageExtent.height, requestedWidth, requestedHeight);
		}

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

		uint32_t modeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
		std::vector<VkPresentModeKHR> modes(modeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data());

		if (formats.empty() || modes.empty())
		{
			LOG_ERROR(Render, "No surface formats or present modes");
			return false;
		}

		VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(formats);
		m_requestedPresentMode = requestedPresentMode;
		m_presentMode = ChoosePresentMode(modes, requestedPresentMode);
		VkPresentModeKHR presentMode = m_presentMode;
		m_extent = ClampExtent(requestedWidth, requestedHeight, caps);
		m_imageFormat = surfaceFormat.format;

		uint32_t imageCount = caps.minImageCount + 1;
		if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
		{
			imageCount = caps.maxImageCount;
		}

		const uint32_t queueFamilyIndices[] = { graphicsQueueFamilyIndex, presentQueueFamilyIndex };
		const bool exclusive = (graphicsQueueFamilyIndex == presentQueueFamilyIndex);

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = m_extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		createInfo.imageSharingMode = exclusive ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = exclusive ? 0u : 2u;
		createInfo.pQueueFamilyIndices = exclusive ? nullptr : queueFamilyIndices;
		createInfo.preTransform = caps.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		result = vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain);
		LOG_INFO(Render, "[SWAPCHAIN] vkCreateSwapchainKHR r={}", (int)result);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "vkCreateSwapchainKHR failed: {}", static_cast<int>(result));
			return false;
		}

		uint32_t swapchainImageCount = 0;
		vkGetSwapchainImagesKHR(device, m_swapchain, &swapchainImageCount, nullptr);
		m_images.resize(swapchainImageCount);
		vkGetSwapchainImagesKHR(device, m_swapchain, &swapchainImageCount, m_images.data());

		m_imageViews.resize(swapchainImageCount);
		for (uint32_t i = 0; i < swapchainImageCount; ++i)
		{
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_images[i];
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = m_imageFormat;
			viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			result = vkCreateImageView(device, &viewInfo, nullptr, &m_imageViews[i]);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "vkCreateImageView failed for image {}: {}", i, static_cast<int>(result));
				Destroy();
				return false;
			}
		}

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = m_imageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo rpInfo{};
		rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments = &colorAttachment;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dependency;

		result = vkCreateRenderPass(device, &rpInfo, nullptr, &m_renderPass);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "vkCreateRenderPass failed: {}", static_cast<int>(result));
			Destroy();
			return false;
		}

		m_framebuffers.resize(swapchainImageCount);
		for (uint32_t i = 0; i < swapchainImageCount; ++i)
		{
			VkFramebufferCreateInfo fbInfo{};
			fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			fbInfo.renderPass = m_renderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments = &m_imageViews[i];
			fbInfo.width = m_extent.width;
			fbInfo.height = m_extent.height;
			fbInfo.layers = 1;

			result = vkCreateFramebuffer(device, &fbInfo, nullptr, &m_framebuffers[i]);
			if (result != VK_SUCCESS)
			{
				LOG_ERROR(Render, "vkCreateFramebuffer failed for {}: {}", i, static_cast<int>(result));
				Destroy();
				return false;
			}
		}

		m_physicalDevice = physicalDevice;
		m_device = device;
		m_surface = surface;
		m_graphicsQueueFamilyIndex = graphicsQueueFamilyIndex;
		m_presentQueueFamilyIndex = presentQueueFamilyIndex;

		LOG_INFO(Render, "[SWAPCHAIN] Create OK images={}", swapchainImageCount);
		const char* modeName = (m_presentMode == VK_PRESENT_MODE_FIFO_KHR) ? "FIFO" :
			(m_presentMode == VK_PRESENT_MODE_MAILBOX_KHR) ? "MAILBOX" :
			(m_presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) ? "IMMEDIATE" : "OTHER";
		LOG_INFO(Render, "VkSwapchain created: {} images, extent {}x{}, presentMode={}", swapchainImageCount, m_extent.width, m_extent.height, modeName);
		return true;
	}

	void VkSwapchain::Destroy()
	{
		LOG_DEBUG(Render, "[SWAPCHAIN] Destroy enter");
		if (m_device == VK_NULL_HANDLE)
		{
			return;
		}

		for (VkFramebuffer fb : m_framebuffers)
		{
			if (fb != VK_NULL_HANDLE)
			{
				vkDestroyFramebuffer(m_device, fb, nullptr);
			}
		}
		m_framebuffers.clear();

		if (m_renderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(m_device, m_renderPass, nullptr);
			m_renderPass = VK_NULL_HANDLE;
		}

		for (VkImageView iv : m_imageViews)
		{
			if (iv != VK_NULL_HANDLE)
			{
				vkDestroyImageView(m_device, iv, nullptr);
			}
		}
		m_imageViews.clear();
		m_images.clear();

		if (m_swapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
			m_swapchain = VK_NULL_HANDLE;
		}

		m_imageFormat = VK_FORMAT_UNDEFINED;
		m_extent = { 0, 0 };
		LOG_INFO(Render, "VkSwapchain destroyed");
	}

	bool VkSwapchain::Recreate(uint32_t requestedWidth, uint32_t requestedHeight)
	{
		if (m_device == VK_NULL_HANDLE || m_physicalDevice == VK_NULL_HANDLE || m_surface == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "VkSwapchain::Recreate: not initialized");
			return false;
		}
		Destroy();
		return Create(m_physicalDevice, m_device, m_surface,
			m_graphicsQueueFamilyIndex, m_presentQueueFamilyIndex,
			requestedWidth, requestedHeight, m_requestedPresentMode);
	}

	VkFramebuffer VkSwapchain::GetFramebuffer(uint32_t imageIndex) const
	{
		if (imageIndex >= m_framebuffers.size())
		{
			return VK_NULL_HANDLE;
		}
		return m_framebuffers[imageIndex];
	}

	VkImage VkSwapchain::GetImage(uint32_t imageIndex) const
	{
		if (imageIndex >= m_images.size())
		{
			return VK_NULL_HANDLE;
		}
		return m_images[imageIndex];
	}

	VkImageView VkSwapchain::GetImageView(uint32_t imageIndex) const
	{
		if (imageIndex >= m_imageViews.size())
		{
			return VK_NULL_HANDLE;
		}
		return m_imageViews[imageIndex];
	}
}
