#include "engine/render/vk/VkDeviceContext.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace engine::render
{
	namespace
	{
		const char* const kSwapchainExtensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

		/// Scores a physical device for suitability (higher = better). Returns 0 if unsuitable.
		int ScorePhysicalDevice(VkPhysicalDevice phys, VkSurfaceKHR surface)
		{
			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(phys, &props);

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(phys, &queueFamilyCount, nullptr);
			if (queueFamilyCount == 0)
			{
				return 0;
			}
			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(phys, &queueFamilyCount, queueFamilies.data());

			uint32_t graphicsIndex = UINT32_MAX;
			uint32_t presentIndex = UINT32_MAX;
			for (uint32_t i = 0; i < queueFamilyCount; ++i)
			{
				if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					graphicsIndex = i;
				}
				if (surface != VK_NULL_HANDLE)
				{
					VkBool32 presentSupport = VK_FALSE;
					vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &presentSupport);
					if (presentSupport == VK_TRUE)
					{
						presentIndex = i;
					}
				}
			}
			if (graphicsIndex == UINT32_MAX || (surface != VK_NULL_HANDLE && presentIndex == UINT32_MAX))
			{
				return 0;
			}

			uint32_t extCount = 0;
			vkEnumerateDeviceExtensionProperties(phys, nullptr, &extCount, nullptr);
			std::vector<VkExtensionProperties> exts(extCount);
			vkEnumerateDeviceExtensionProperties(phys, nullptr, &extCount, exts.data());
			bool hasSwapchain = false;
			for (const auto& e : exts)
			{
				if (std::strcmp(e.extensionName, kSwapchainExtensionName) == 0)
				{
					hasSwapchain = true;
					break;
				}
			}
			if (!hasSwapchain)
			{
				return 0;
			}

			if (surface != VK_NULL_HANDLE)
			{
				uint32_t formatCount = 0;
				vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &formatCount, nullptr);
				uint32_t modeCount = 0;
				vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &modeCount, nullptr);
				if (formatCount == 0 || modeCount == 0)
				{
					return 0;
				}
			}

			int score = 1;
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
			{
				score += 1000;
			}
			else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
			{
				score += 100;
			}
			return score;
		}

		/// Fills graphics and present queue family indices. Returns false if not found.
		bool FindQueueFamilies(VkPhysicalDevice phys, VkSurfaceKHR surface,
			uint32_t& outGraphics, uint32_t& outPresent)
		{
			outGraphics = VkDeviceContext::kInvalidQueueFamily;
			outPresent = VkDeviceContext::kInvalidQueueFamily;

			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(phys, &queueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(phys, &queueFamilyCount, queueFamilies.data());

			for (uint32_t i = 0; i < queueFamilyCount; ++i)
			{
				if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					outGraphics = i;
				}
				if (surface != VK_NULL_HANDLE)
				{
					VkBool32 presentSupport = VK_FALSE;
					vkGetPhysicalDeviceSurfaceSupportKHR(phys, i, surface, &presentSupport);
					if (presentSupport == VK_TRUE)
					{
						outPresent = i;
					}
				}
			}
			return outGraphics != VkDeviceContext::kInvalidQueueFamily
				&& (surface == VK_NULL_HANDLE || outPresent != VkDeviceContext::kInvalidQueueFamily);
		}
	}

	bool VkDeviceContext::Create(::VkInstance instance, VkSurfaceKHR surface)
	{
		if (instance == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "VkDeviceContext::Create: invalid instance");
			return false;
		}

		uint32_t deviceCount = 0;
		VkResult result = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
		if (result != VK_SUCCESS || deviceCount == 0)
		{
			LOG_ERROR(Render, "vkEnumeratePhysicalDevices failed or no devices: {}", static_cast<int>(result));
			return false;
		}
		std::vector<VkPhysicalDevice> devices(deviceCount);
		result = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "vkEnumeratePhysicalDevices failed: {}", static_cast<int>(result));
			return false;
		}

		int bestScore = 0;
		VkPhysicalDevice bestPhys = VK_NULL_HANDLE;
		for (VkPhysicalDevice phys : devices)
		{
			int score = ScorePhysicalDevice(phys, surface);
			if (score > bestScore)
			{
				bestScore = score;
				bestPhys = phys;
			}
		}
		if (bestPhys == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "No suitable physical device found (graphics+present+swapchain)");
			return false;
		}

		m_physicalDevice = bestPhys;

		if (!FindQueueFamilies(m_physicalDevice, surface, m_graphicsQueueFamilyIndex, m_presentQueueFamilyIndex))
		{
			LOG_ERROR(Render, "Queue families not found");
			m_physicalDevice = VK_NULL_HANDLE;
			return false;
		}

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		const float queuePriority = 1.0f;
		if (m_graphicsQueueFamilyIndex == m_presentQueueFamilyIndex)
		{
			VkDeviceQueueCreateInfo qi{};
			qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			qi.queueFamilyIndex = m_graphicsQueueFamilyIndex;
			qi.queueCount = 1;
			qi.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(qi);
		}
		else
		{
			VkDeviceQueueCreateInfo qiGraphics{};
			qiGraphics.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			qiGraphics.queueFamilyIndex = m_graphicsQueueFamilyIndex;
			qiGraphics.queueCount = 1;
			qiGraphics.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(qiGraphics);

			VkDeviceQueueCreateInfo qiPresent{};
			qiPresent.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			qiPresent.queueFamilyIndex = m_presentQueueFamilyIndex;
			qiPresent.queueCount = 1;
			qiPresent.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(qiPresent);
		}

		VkPhysicalDeviceFeatures deviceFeatures{};

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = 1;
		createInfo.ppEnabledExtensionNames = &kSwapchainExtensionName;

		result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "vkCreateDevice failed: {}", static_cast<int>(result));
			m_physicalDevice = VK_NULL_HANDLE;
			return false;
		}

		vkGetDeviceQueue(m_device, m_graphicsQueueFamilyIndex, 0, &m_graphicsQueue);
		vkGetDeviceQueue(m_device, m_presentQueueFamilyIndex, 0, &m_presentQueue);

		LOG_INFO(Render, "VkDeviceContext created (graphics queue family {}, present queue family {})",
			m_graphicsQueueFamilyIndex, m_presentQueueFamilyIndex);
		return true;
	}

	void VkDeviceContext::Destroy()
	{
		if (m_device == VK_NULL_HANDLE)
		{
			return;
		}
		vkDestroyDevice(m_device, nullptr);
		m_device = VK_NULL_HANDLE;
		m_physicalDevice = VK_NULL_HANDLE;
		m_graphicsQueue = VK_NULL_HANDLE;
		m_presentQueue = VK_NULL_HANDLE;
		m_graphicsQueueFamilyIndex = kInvalidQueueFamily;
		m_presentQueueFamilyIndex = kInvalidQueueFamily;
		LOG_INFO(Render, "VkDeviceContext destroyed");
	}
}
