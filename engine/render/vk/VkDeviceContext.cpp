#include "engine/render/vk/VkDeviceContext.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace engine::render
{
	namespace
	{
		const char* const kSwapchainExtensionName = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
		const char* const kSynchronization2ExtensionName = VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
		const char* const kDescriptorIndexingExtensionName = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;

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
		LOG_DEBUG(Render, "[VKDEV] Create enter");
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

		VkPhysicalDeviceProperties physProps{};
		vkGetPhysicalDeviceProperties(m_physicalDevice, &physProps);
		uint32_t apiVersion = physProps.apiVersion;

		uint32_t extCount = 0;
		vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
		std::vector<VkExtensionProperties> deviceExts(extCount);
		vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, deviceExts.data());
		bool hasSync2Ext = false;
		bool hasDescriptorIndexingExt = false;
		for (const auto& e : deviceExts)
		{
			if (std::strcmp(e.extensionName, kSynchronization2ExtensionName) == 0)
			{
				hasSync2Ext = true;
			}
			if (std::strcmp(e.extensionName, kDescriptorIndexingExtensionName) == 0)
			{
				hasDescriptorIndexingExt = true;
			}
		}
		bool wantSync2 = (VK_VERSION_MAJOR(apiVersion) > 1 || (VK_VERSION_MAJOR(apiVersion) == 1 && VK_VERSION_MINOR(apiVersion) >= 3))
			|| hasSync2Ext;
		const bool descriptorIndexingInCore = (VK_VERSION_MAJOR(apiVersion) > 1) || (VK_VERSION_MAJOR(apiVersion) == 1 && VK_VERSION_MINOR(apiVersion) >= 2);
		const bool canUseDescriptorIndexing = descriptorIndexingInCore || hasDescriptorIndexingExt;

		std::vector<const char*> enabledExtensions;
		enabledExtensions.push_back(kSwapchainExtensionName);
		bool apiPre13 = (VK_VERSION_MAJOR(apiVersion) < 1) || (VK_VERSION_MAJOR(apiVersion) == 1 && VK_VERSION_MINOR(apiVersion) < 3);
		if (wantSync2 && apiPre13 && hasSync2Ext)
		{
			enabledExtensions.push_back(kSynchronization2ExtensionName);
		}
		if (canUseDescriptorIndexing && !descriptorIndexingInCore && hasDescriptorIndexingExt)
		{
			enabledExtensions.push_back(kDescriptorIndexingExtensionName);
		}

		VkPhysicalDeviceFeatures2 featureQuery{};
		featureQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
		descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		featureQuery.pNext = &descriptorIndexingFeatures;
		vkGetPhysicalDeviceFeatures2(m_physicalDevice, &featureQuery);

		VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features{};
		sync2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
		sync2Features.synchronization2 = VK_TRUE;

		deviceFeatures.robustBufferAccess              = featureQuery.features.robustBufferAccess;
		deviceFeatures.shaderStorageImageExtendedFormats = featureQuery.features.shaderStorageImageExtendedFormats;

		void* featureChain = nullptr;
		if (canUseDescriptorIndexing
			&& descriptorIndexingFeatures.runtimeDescriptorArray
			&& descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing
			&& descriptorIndexingFeatures.descriptorBindingPartiallyBound)
		{
			descriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
			descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
			descriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
			featureChain = &descriptorIndexingFeatures;
			m_descriptorIndexingSupported = true;
		}
		else
		{
			m_descriptorIndexingSupported = false;
		}
		m_dynamicRenderingSupported = false;

		if (wantSync2)
		{
			sync2Features.pNext = featureChain;
			featureChain = &sync2Features;
		}

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.pNext = featureChain;
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos = queueCreateInfos.data();
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
		createInfo.ppEnabledExtensionNames = enabledExtensions.data();

		result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
		LOG_INFO(Render, "[VKDEV] vkCreateDevice r={} device={}", (int)result, (void*)m_device);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "vkCreateDevice failed: {}", static_cast<int>(result));
			m_physicalDevice = VK_NULL_HANDLE;
			return false;
		}

		m_sync2Supported = wantSync2;

		vkGetDeviceQueue(m_device, m_graphicsQueueFamilyIndex, 0, &m_graphicsQueue);
		vkGetDeviceQueue(m_device, m_presentQueueFamilyIndex, 0, &m_presentQueue);

		LOG_INFO(Render, "VkDeviceContext created (graphics queue family {}, present queue family {}, sync2: {}, descriptor_indexing: {}, dynamic_rendering: {}, robust_buffer_access: {})",
			m_graphicsQueueFamilyIndex,
			m_presentQueueFamilyIndex,
			m_sync2Supported ? "yes" : "no",
			m_descriptorIndexingSupported ? "yes" : "no",
			m_dynamicRenderingSupported ? "yes" : "no",
			deviceFeatures.robustBufferAccess ? "yes" : "no");
		if (!m_descriptorIndexingSupported)
			LOG_WARN(Render, "[VkDeviceContext] Descriptor indexing unavailable; bindless path may be limited");
		if (!m_dynamicRenderingSupported)
			LOG_WARN(Render, "[VkDeviceContext] Dynamic rendering unavailable; UI overlay fallback will be used");
		return true;
	}

	void VkDeviceContext::Destroy()
	{
		LOG_DEBUG(Render, "[VKDEV] Destroy enter");
		if (m_device == VK_NULL_HANDLE)
		{
			LOG_INFO(Render, "VkDeviceContext destroyed");
			return;
		}
		vkDestroyDevice(m_device, nullptr);
		LOG_INFO(Render, "[VKDEV] Destroy OK");
		m_device = VK_NULL_HANDLE;
		m_physicalDevice = VK_NULL_HANDLE;
		m_graphicsQueue = VK_NULL_HANDLE;
		m_presentQueue = VK_NULL_HANDLE;
		m_graphicsQueueFamilyIndex = kInvalidQueueFamily;
		m_presentQueueFamilyIndex = kInvalidQueueFamily;
		m_sync2Supported = false;
		m_descriptorIndexingSupported = false;
		m_dynamicRenderingSupported = false;
		LOG_INFO(Render, "VkDeviceContext destroyed");
	}
}
