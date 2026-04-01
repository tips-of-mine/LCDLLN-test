#include "engine/render/vk/VkInstance.h"
#include "engine/core/Log.h"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <vulkan/vulkan.h>

#include <cstring>
#include <vector>

namespace engine::render
{
	namespace
	{
#ifndef NDEBUG
		const char* const kValidationLayerName = "VK_LAYER_KHRONOS_validation";
		const char* const kDebugUtilsExtensionName = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

		VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* /*pUserData*/)
		{
			// Do not call any Vulkan API inside the callback.
			const char* msg = pCallbackData->pMessage ? pCallbackData->pMessage : "";
			if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				engine::core::Log::WriteLine(engine::core::LogLevel::Error, "Render", msg);
			}
			else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				engine::core::Log::WriteLine(engine::core::LogLevel::Warn, "Render", msg);
			}
			return VK_FALSE;
		}
#endif
	}

	bool VkInstance::Create()
	{
		uint32_t extCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extCount);
		if (!glfwExtensions || extCount == 0)
		{
			LOG_ERROR(Render, "glfwGetRequiredInstanceExtensions failed");
			return false;
		}

		std::vector<const char*> extensions(glfwExtensions, glfwExtensions + extCount);

#ifndef NDEBUG
		extensions.push_back(kDebugUtilsExtensionName);
#endif

		std::vector<const char*> layers;
#ifndef NDEBUG
		layers.push_back(kValidationLayerName);
#endif

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "LCDLLN Engine";
		appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.pEngineName = "LCDLLN";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.apiVersion = VK_API_VERSION_1_2;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();
		createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
		createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

		VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "vkCreateInstance failed: {}", static_cast<int>(result));
			return false;
		}

#ifndef NDEBUG
		VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
		messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		messengerInfo.messageSeverity =
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		messengerInfo.messageType =
			VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		messengerInfo.pfnUserCallback = &DebugMessengerCallback;

		auto vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
		if (vkCreateDebugUtilsMessengerEXT)
		{
			result = vkCreateDebugUtilsMessengerEXT(m_instance, &messengerInfo, nullptr, &m_debugMessenger);
			if (result != VK_SUCCESS)
			{
				LOG_WARN(Render, "vkCreateDebugUtilsMessengerEXT failed: {}", static_cast<int>(result));
				m_debugMessenger = VK_NULL_HANDLE;
			}
		}
#endif

		LOG_INFO(Render, "Vulkan instance created");
		return true;
	}

	bool VkInstance::CreateSurface(GLFWwindow* window)
	{
		if (!window)
		{
			return true;
		}
		if (m_instance == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "CreateSurface: instance not created");
			return false;
		}
		VkResult result = glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "glfwCreateWindowSurface failed: {}", static_cast<int>(result));
			return false;
		}
		LOG_INFO(Render, "Vulkan surface created");
		return true;
	}

	bool VkInstance::CreateSurface(void* nativeWindowHandle)
	{
		if (!nativeWindowHandle)
		{
			return true;
		}
		if (m_instance == VK_NULL_HANDLE)
		{
			LOG_ERROR(Render, "CreateSurface(native): instance not created");
			return false;
		}
#if defined(_WIN32)
		VkWin32SurfaceCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		createInfo.hinstance = GetModuleHandleW(nullptr);
		createInfo.hwnd = reinterpret_cast<HWND>(nativeWindowHandle);
		const VkResult result = vkCreateWin32SurfaceKHR(m_instance, &createInfo, nullptr, &m_surface);
		if (result != VK_SUCCESS)
		{
			LOG_ERROR(Render, "vkCreateWin32SurfaceKHR failed: {}", static_cast<int>(result));
			return false;
		}
		LOG_INFO(Render, "Vulkan Win32 surface created");
		return true;
#else
		(void)nativeWindowHandle;
		LOG_ERROR(Render, "CreateSurface(native) unsupported on this platform");
		return false;
#endif
	}

	void VkInstance::Destroy()
	{
		if (m_instance == VK_NULL_HANDLE)
		{
			return;
		}

		if (m_surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
			m_surface = VK_NULL_HANDLE;
		}

#ifndef NDEBUG
		if (m_debugMessenger != VK_NULL_HANDLE)
		{
			auto vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
			if (vkDestroyDebugUtilsMessengerEXT)
			{
				vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
			}
			m_debugMessenger = VK_NULL_HANDLE;
		}
#endif

		vkDestroyInstance(m_instance, nullptr);
		m_instance = VK_NULL_HANDLE;
		LOG_INFO(Render, "Vulkan instance destroyed");
	}
}
