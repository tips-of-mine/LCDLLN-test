/**
 * @file VkSsaoKernelNoise.cpp
 * @brief SSAO kernel + 4x4 noise generation and upload (UBO + texture).
 *
 * Ticket: M06.1 — SSAO: kernel + noise generation.
 */

#include "engine/render/vk/VkSsaoKernelNoise.h"
#include "engine/core/Log.h"

#include <cmath>
#include <cstring>
#include <random>

namespace engine::render::vk {

namespace {

constexpr uint32_t kNoiseSize = 4u;
constexpr uint64_t kKernelSeed = 12345u;
constexpr uint64_t kNoiseSeed  = 67890u;

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice,
                        uint32_t typeFilter,
                        VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return UINT32_MAX;
}

float ClampRadius(float r) {
    if (r < 0.01f) return 0.01f;
    if (r > 2.0f) return 2.0f;
    return r;
}

float ClampBias(float b) {
    if (b < 0.0001f) return 0.0001f;
    if (b > 0.1f) return 0.1f;
    return b;
}

} // namespace

VkSsaoKernelNoise::~VkSsaoKernelNoise() {
    Shutdown();
}

void VkSsaoKernelNoise::DestroyResources() {
    if (m_device == VK_NULL_HANDLE) return;

    if (m_noiseSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_noiseSampler, nullptr);
        m_noiseSampler = VK_NULL_HANDLE;
    }
    if (m_noiseView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_noiseView, nullptr);
        m_noiseView = VK_NULL_HANDLE;
    }
    if (m_noiseImage != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, m_noiseImage, nullptr);
        m_noiseImage = VK_NULL_HANDLE;
    }
    if (m_noiseMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_noiseMemory, nullptr);
        m_noiseMemory = VK_NULL_HANDLE;
    }
    if (m_kernelBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_device, m_kernelBuffer, nullptr);
        m_kernelBuffer = VK_NULL_HANDLE;
    }
    if (m_kernelMemory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, m_kernelMemory, nullptr);
        m_kernelMemory = VK_NULL_HANDLE;
    }
}

bool VkSsaoKernelNoise::Init(VkPhysicalDevice physicalDevice,
                             VkDevice         device,
                             VkQueue          queue,
                             VkCommandPool    commandPool,
                             float            radius,
                             float            bias) {
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE ||
        queue == VK_NULL_HANDLE || commandPool == VK_NULL_HANDLE) {
        return false;
    }

    radius = ClampRadius(radius);
    bias   = ClampBias(bias);

    m_physicalDevice = physicalDevice;
    m_device         = device;

    // --- Kernel: 32 hemisphere samples biased toward center (theta = acos(1 - rand^2)) ---
    std::mt19937 gen(static_cast<std::mt19937::result_type>(kKernelSeed));
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    alignas(16) struct {
        float samples[kSsaoKernelSize][4];  // vec3 + padding per std140
        float radius;
        float bias;
        float _pad[2];
    } kernelData;
    std::memset(&kernelData, 0, sizeof(kernelData));

    for (uint32_t i = 0; i < kSsaoKernelSize; ++i) {
        float u1 = dist(gen);
        float u2 = dist(gen);
        float theta = std::acos(1.0f - u1 * u1);  // bias toward apex (center)
        float phi   = 2.0f * 3.14159265f * u2;
        float x = std::sin(theta) * std::cos(phi);
        float y = std::sin(theta) * std::sin(phi);
        float z = std::cos(theta);
        float len = std::sqrt(x * x + y * y + z * z);
        if (len > 1e-6f) { x /= len; y /= len; z /= len; }
        kernelData.samples[i][0] = x;
        kernelData.samples[i][1] = y;
        kernelData.samples[i][2] = z;
        kernelData.samples[i][3] = 0.0f;
    }
    kernelData.radius = radius;
    kernelData.bias   = bias;

    VkBufferCreateInfo bci{};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = kSsaoKernelUBOSize;
    bci.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &m_kernelBuffer) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSsaoKernelNoise: kernel UBO create failed");
        DestroyResources();
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, m_kernelBuffer, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &allocInfo, nullptr, &m_kernelMemory) != VK_SUCCESS ||
        vkBindBufferMemory(device, m_kernelBuffer, m_kernelMemory, 0) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSsaoKernelNoise: kernel UBO memory failed");
        DestroyResources();
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device, m_kernelMemory, 0, kSsaoKernelUBOSize, 0, &mapped) != VK_SUCCESS) {
        DestroyResources();
        return false;
    }
    std::memcpy(mapped, &kernelData, sizeof(kernelData));
    vkUnmapMemory(device, m_kernelMemory);

    // --- Noise: 4x4 RG8, random XY (tiled for TBN rotation) ---
    std::mt19937 noiseGen(static_cast<std::mt19937::result_type>(kNoiseSeed));
    std::uniform_real_distribution<float> noiseDist(-1.0f, 1.0f);

    uint8_t noisePixels[kNoiseSize * kNoiseSize * 2];
    for (uint32_t i = 0; i < kNoiseSize * kNoiseSize * 2; i += 2) {
        float x = noiseDist(noiseGen);
        float y = noiseDist(noiseGen);
        noisePixels[i + 0] = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(x * 127.5f + 127.5f))));
        noisePixels[i + 1] = static_cast<uint8_t>(std::max(0, std::min(255, static_cast<int>(y * 127.5f + 127.5f))));
    }

    VkDeviceSize noiseDataSize = kNoiseSize * kNoiseSize * 2 * sizeof(uint8_t);

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo sbci{};
    sbci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbci.size        = noiseDataSize;
    sbci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    sbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &sbci, nullptr, &stagingBuffer) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSsaoKernelNoise: noise staging buffer failed");
        DestroyResources();
        return false;
    }

    VkMemoryRequirements stagingReqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &stagingReqs);
    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize  = stagingReqs.size;
    stagingAlloc.memoryTypeIndex = FindMemoryType(physicalDevice, stagingReqs.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &stagingAlloc, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyResources();
        return false;
    }

    void* stagingMapped = nullptr;
    if (vkMapMemory(device, stagingMemory, 0, noiseDataSize, 0, &stagingMapped) != VK_SUCCESS) {
        vkFreeMemory(device, stagingMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyResources();
        return false;
    }
    std::memcpy(stagingMapped, noisePixels, static_cast<size_t>(noiseDataSize));
    vkUnmapMemory(device, stagingMemory);

    VkImageCreateInfo ici{};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8_UNORM;
    ici.extent        = { kNoiseSize, kNoiseSize, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ici, nullptr, &m_noiseImage) != VK_SUCCESS) {
        vkFreeMemory(device, stagingMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyResources();
        return false;
    }

    vkGetImageMemoryRequirements(device, m_noiseImage, &memReqs);
    allocInfo.allocationSize  = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memReqs.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(device, &allocInfo, nullptr, &m_noiseMemory) != VK_SUCCESS ||
        vkBindImageMemory(device, m_noiseImage, m_noiseMemory, 0) != VK_SUCCESS) {
        vkDestroyImage(device, m_noiseImage, nullptr);
        m_noiseImage = VK_NULL_HANDLE;
        vkFreeMemory(device, stagingMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyResources();
        return false;
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool        = commandPool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &cbai, &cmd) != VK_SUCCESS) {
        vkFreeMemory(device, m_noiseMemory, nullptr);
        vkDestroyImage(device, m_noiseImage, nullptr);
        m_noiseImage = VK_NULL_HANDLE;
        m_noiseMemory = VK_NULL_HANDLE;
        vkFreeMemory(device, stagingMemory, nullptr);
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        DestroyResources();
        return false;
    }

    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);

    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = m_noiseImage;
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.imageOffset       = { 0, 0, 0 };
    region.imageExtent       = { kNoiseSize, kNoiseSize, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_noiseImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    VkImageViewCreateInfo ivci{};
    ivci.sType             = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image             = m_noiseImage;
    ivci.viewType          = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format            = VK_FORMAT_R8G8_UNORM;
    ivci.subresourceRange  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device, &ivci, nullptr, &m_noiseView) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSsaoKernelNoise: noise view failed");
        DestroyResources();
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter     = VK_FILTER_NEAREST;
    sci.minFilter     = VK_FILTER_NEAREST;
    sci.addressModeU  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW  = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (vkCreateSampler(device, &sci, nullptr, &m_noiseSampler) != VK_SUCCESS) {
        LOG_ERROR(Render, "VkSsaoKernelNoise: noise sampler failed");
        DestroyResources();
        return false;
    }

    return true;
}

void VkSsaoKernelNoise::Shutdown() {
    DestroyResources();
    m_physicalDevice = VK_NULL_HANDLE;
    m_device         = VK_NULL_HANDLE;
}

} // namespace engine::render::vk
