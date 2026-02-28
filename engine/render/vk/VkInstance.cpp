#include "engine/render/vk/VkInstance.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "engine/core/Log.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace engine::core;

namespace engine::render::vk {

namespace {

#if defined(NDEBUG)
constexpr bool kEnableValidationLayers = false;
#else
constexpr bool kEnableValidationLayers = true;
#endif

constexpr std::array<const char*, 1> kValidationLayers = {
    "VK_LAYER_KHRONOS_validation",
};

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             /*messageTypes*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void*                                       /*pUserData*/) {

    const char* msg = pCallbackData && pCallbackData->pMessage
                        ? pCallbackData->pMessage
                        : "(no message)";

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_ERROR(Render, "Vulkan validation: {}", msg);
    } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARN(Render, "Vulkan validation: {}", msg);
    }

    return VK_FALSE;
}

void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& ci) {
    std::memset(&ci, 0, sizeof(ci));
    ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = DebugCallback;
    ci.pUserData       = nullptr;
}

bool CheckValidationLayerSupport() {
    uint32_t layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkEnumerateInstanceLayerProperties failed");
        return false;
    }

    std::vector<VkLayerProperties> available(layerCount);
    if (layerCount > 0) {
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());
    }

    for (const char* layerName : kValidationLayers) {
        bool found = false;
        for (const auto& props : available) {
            if (std::strcmp(props.layerName, layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOG_WARN(Render, "Validation layer '{}' not available; disabling validation", layerName);
            return false;
        }
    }

    return true;
}

std::vector<const char*> CollectRequiredExtensions(bool enableDebug) {
    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    if (!glfwExts || glfwExtCount == 0) {
        LOG_ERROR(Render, "glfwGetRequiredInstanceExtensions returned no extensions");
        return {};
    }

    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

    if (enableDebug) {
        uint32_t extCount = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkEnumerateInstanceExtensionProperties failed");
            return {};
        }

        std::vector<VkExtensionProperties> available(extCount);
        if (extCount > 0) {
            vkEnumerateInstanceExtensionProperties(nullptr, &extCount, available.data());
        }

        const char* debugExt = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        bool        found    = false;
        for (const auto& e : available) {
            if (std::strcmp(e.extensionName, debugExt) == 0) {
                found = true;
                break;
            }
        }

        if (found) {
            extensions.push_back(debugExt);
        } else {
            LOG_WARN(Render, "VK_EXT_debug_utils not available; disabling debug messenger");
        }
    }

    return extensions;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance                                instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    VkDebugUtilsMessengerEXT*                 messenger) {
    const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!func) {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
    return func(instance, createInfo, nullptr, messenger);
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance                   instance,
    VkDebugUtilsMessengerEXT     messenger) {
    if (messenger == VK_NULL_HANDLE || instance == VK_NULL_HANDLE) {
        return;
    }
    const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func) {
        func(instance, messenger, nullptr);
    }
}

} // namespace

Instance::~Instance() {
    Shutdown();
}

bool Instance::Init(GLFWwindow* window) {
    if (m_instance != VK_NULL_HANDLE) {
        return true;
    }

    const bool enableValidation = kEnableValidationLayers && CheckValidationLayerSupport();

    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "MMORPG Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "MMORPG Engine";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_2;

    std::vector<const char*> extensions = CollectRequiredExtensions(enableValidation);
    if (extensions.empty()) {
        return false;
    }

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCi{};
    if (enableValidation) {
        ci.enabledLayerCount   = static_cast<uint32_t>(kValidationLayers.size());
        ci.ppEnabledLayerNames = kValidationLayers.data();

        PopulateDebugMessengerCreateInfo(debugCi);
        ci.pNext = &debugCi;
    } else {
        ci.enabledLayerCount   = 0;
        ci.ppEnabledLayerNames = nullptr;
        ci.pNext               = nullptr;
    }

    const VkResult res = vkCreateInstance(&ci, nullptr, &m_instance);
    if (res != VK_SUCCESS || m_instance == VK_NULL_HANDLE) {
        LOG_ERROR(Render, "vkCreateInstance failed (code {})", static_cast<int>(res));
        m_instance = VK_NULL_HANDLE;
        return false;
    }

    LOG_INFO(Render, "Vulkan instance created (validation={})",
             enableValidation ? "on" : "off");

    if (enableValidation) {
        const VkResult dbgRes = CreateDebugUtilsMessengerEXT(m_instance, &debugCi, &m_debugMessenger);
        if (dbgRes != VK_SUCCESS || m_debugMessenger == VK_NULL_HANDLE) {
            LOG_WARN(Render, "CreateDebugUtilsMessengerEXT failed (code {}); continuing without messenger",
                     static_cast<int>(dbgRes));
            m_debugMessenger = VK_NULL_HANDLE;
        } else {
            LOG_INFO(Render, "Vulkan debug messenger installed");
        }
    }

    if (window) {
        const VkResult surfRes =
            glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface);
        if (surfRes != VK_SUCCESS || m_surface == VK_NULL_HANDLE) {
            LOG_ERROR(Render, "glfwCreateWindowSurface failed (code {})",
                      static_cast<int>(surfRes));
            m_surface = VK_NULL_HANDLE;
            return false;
        }

        LOG_INFO(Render, "Vulkan surface created for GLFW window");
    }

    return true;
}

void Instance::Shutdown() {
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_debugMessenger != VK_NULL_HANDLE) {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
        LOG_INFO(Render, "Vulkan instance destroyed");
    }
}

} // namespace engine::render::vk

