#include "engine/render/vk/VkDeviceContext.h"

#include "engine/core/Log.h"

#include <vector>

using namespace engine::core;

namespace engine::render::vk {

namespace {

// Single required device extension for this ticket.
constexpr const char* kRequiredDeviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

// Scores a physical device: higher is better. Returns 0 if unsuitable.
std::uint32_t ScorePhysicalDevice(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(device, &props);

    std::uint32_t score = 0;

    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000; // Strong preference for discrete GPU.
    } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 500;
    }

    // Slight preference for higher max image dimension (proxy for capability).
    score += props.limits.maxImageDimension2D;

    return score;
}

// Checks that the device supports all required extensions.
bool CheckDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extCount = 0;
    if (vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, nullptr) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkEnumerateDeviceExtensionProperties failed");
        return false;
    }

    std::vector<VkExtensionProperties> available(extCount);
    if (extCount > 0) {
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extCount, available.data());
    }

    for (const char* required : kRequiredDeviceExtensions) {
        bool found = false;
        for (const auto& ext : available) {
            if (std::strcmp(ext.extensionName, required) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            LOG_WARN(Render, "Device missing required extension '{}'", required);
            return false;
        }
    }

    return true;
}

// Queries whether the device supports at least one surface format and one present mode.
bool CheckSwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    uint32_t formatCount      = 0;
    uint32_t presentModeCount = 0;

    if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkGetPhysicalDeviceSurfaceFormatsKHR failed");
        return false;
    }

    if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr) != VK_SUCCESS) {
        LOG_ERROR(Render, "vkGetPhysicalDeviceSurfacePresentModesKHR failed");
        return false;
    }

    if (formatCount == 0 || presentModeCount == 0) {
        LOG_WARN(Render, "Device has no compatible surface formats or present modes");
        return false;
    }

    return true;
}

// Finds queue families for graphics and present; returns false if not found.
bool FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, QueueFamilyIndices& out) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    if (count == 0) {
        return false;
    }

    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props.data());

    bool foundGraphics = false;
    bool foundPresent  = false;

    for (uint32_t i = 0; i < count; ++i) {
        if (!foundGraphics && (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
            out.graphicsFamily = i;
            foundGraphics      = true;
        }

        VkBool32 presentSupport = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport) == VK_SUCCESS &&
            presentSupport == VK_TRUE) {
            out.presentFamily = i;
            foundPresent      = true;
        }
    }

    if (!foundGraphics || !foundPresent) {
        return false;
    }

    out.sameFamily = (out.graphicsFamily == out.presentFamily);
    return true;
}

} // namespace

VkDeviceContext::~VkDeviceContext() {
    Shutdown();
}

bool VkDeviceContext::Init(VkInstance instance, VkSurfaceKHR surface) {
    if (m_device != VK_NULL_HANDLE) {
        return true;
    }

    m_instance = instance;
    m_surface  = surface;

    // Enumerate physical devices.
    uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0) {
        LOG_ERROR(Render, "vkEnumeratePhysicalDevices failed or returned no devices");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // Select the best suitable device.
    std::uint32_t bestScore = 0;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    QueueFamilyIndices bestIndices{};

    for (VkPhysicalDevice dev : devices) {
        QueueFamilyIndices indices{};
        if (!FindQueueFamilies(dev, surface, indices)) {
            continue;
        }

        if (!CheckDeviceExtensionSupport(dev)) {
            continue;
        }

        if (!CheckSwapchainSupport(dev, surface)) {
            continue;
        }

        const std::uint32_t score = ScorePhysicalDevice(dev);
        if (score > bestScore) {
            bestScore   = score;
            bestDevice  = dev;
            bestIndices = indices;
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        LOG_ERROR(Render, "No suitable Vulkan GPU found (graphics+present+swapchain required)");
        return false;
    }

    m_physicalDevice = bestDevice;
    m_indices        = bestIndices;

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);

    LOG_INFO(Render, "Selected Vulkan GPU: '{}'", props.deviceName);
    LOG_INFO(Render, "Queue families — graphics={} present={} sameFamily={}",
             m_indices.graphicsFamily,
             m_indices.presentFamily,
             m_indices.sameFamily ? "true" : "false");

    // Create logical device and queues.
    const std::uint32_t uniqueFamilies[2] = {
        m_indices.graphicsFamily,
        m_indices.presentFamily
    };

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(2);

    float priority = 1.0f;
    for (int i = 0; i < 2; ++i) {
        if (i == 1 && m_indices.sameFamily) {
            break; // Avoid duplicate entry when families are the same.
        }

        VkDeviceQueueCreateInfo qci{};
        qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = uniqueFamilies[i];
        qci.queueCount       = 1;
        qci.pQueuePriorities = &priority;
        queueInfos.push_back(qci);
    }

    VkPhysicalDeviceFeatures features{};
    // For this ticket we rely on core features; no additional toggles required.

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    dci.pQueueCreateInfos       = queueInfos.data();
    dci.pEnabledFeatures        = &features;
    dci.enabledExtensionCount   = static_cast<uint32_t>(std::size(kRequiredDeviceExtensions));
    dci.ppEnabledExtensionNames = kRequiredDeviceExtensions;
    dci.enabledLayerCount       = 0;
    dci.ppEnabledLayerNames     = nullptr;

    const VkResult res = vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device);
    if (res != VK_SUCCESS || m_device == VK_NULL_HANDLE) {
        LOG_ERROR(Render, "vkCreateDevice failed (code {})", static_cast<int>(res));
        m_device = VK_NULL_HANDLE;
        return false;
    }

    vkGetDeviceQueue(m_device, m_indices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_indices.presentFamily,  0, &m_presentQueue);

    LOG_INFO(Render, "Logical device created (VK_KHR_swapchain enabled)");

    return true;
}

void VkDeviceContext::Shutdown() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
        vkDestroyDevice(m_device, nullptr);
        m_device        = VK_NULL_HANDLE;
        m_graphicsQueue = VK_NULL_HANDLE;
        m_presentQueue  = VK_NULL_HANDLE;
        LOG_INFO(Render, "Vulkan logical device destroyed");
    }
}

} // namespace engine::render::vk

