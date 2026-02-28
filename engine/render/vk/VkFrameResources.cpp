#include "engine/render/vk/VkFrameResources.h"

#include "engine/core/Log.h"

namespace engine::render::vk {

VkFrameResources::~VkFrameResources() {
    Shutdown();
}

bool VkFrameResources::Init(VkDevice device, uint32_t graphicsQueueFamilyIndex) {
    m_device = device;

    for (FrameResources& fr : m_resources) {
        VkCommandPoolCreateInfo cpci{};
        cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cpci.queueFamilyIndex = graphicsQueueFamilyIndex;
        cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device, &cpci, nullptr, &fr.cmdPool) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkCreateCommandPool failed");
            Shutdown();
            return false;
        }

        VkCommandBufferAllocateInfo cbai{};
        cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbai.commandPool        = fr.cmdPool;
        cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device, &cbai, &fr.cmdBuffer) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkAllocateCommandBuffers failed");
            Shutdown();
            return false;
        }

        VkSemaphoreCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(device, &sci, nullptr, &fr.imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(device, &sci, nullptr, &fr.renderFinished) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkCreateSemaphore failed");
            Shutdown();
            return false;
        }

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        if (vkCreateFence(device, &fci, nullptr, &fr.inFlightFence) != VK_SUCCESS) {
            LOG_ERROR(Render, "vkCreateFence failed");
            Shutdown();
            return false;
        }
    }

    LOG_INFO(Render, "FrameResources[2] created (cmdPool/cmd/semaphores/fence per frame)");
    return true;
}

void VkFrameResources::Shutdown() {
    if (m_device == VK_NULL_HANDLE) { return; }

    vkDeviceWaitIdle(m_device);

    for (FrameResources& fr : m_resources) {
        if (fr.inFlightFence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, fr.inFlightFence, nullptr);
            fr.inFlightFence = VK_NULL_HANDLE;
        }
        if (fr.renderFinished != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, fr.renderFinished, nullptr);
            fr.renderFinished = VK_NULL_HANDLE;
        }
        if (fr.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, fr.imageAvailable, nullptr);
            fr.imageAvailable = VK_NULL_HANDLE;
        }
        if (fr.cmdPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_device, fr.cmdPool, nullptr);
            fr.cmdPool   = VK_NULL_HANDLE;
            fr.cmdBuffer = VK_NULL_HANDLE;
        }
    }

    m_device = VK_NULL_HANDLE;
    LOG_INFO(Render, "FrameResources destroyed");
}

} // namespace engine::render::vk
