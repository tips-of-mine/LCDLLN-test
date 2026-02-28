/**
 * @file VkTextureLoader.cpp
 * @brief Texture loading via stb_image + GPU upload + path cache.
 */

#include "engine/render/vk/VkTextureLoader.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <optional>

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb/stb_image.h"

namespace engine::render::vk {

namespace {

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

} // namespace

VkTextureLoader::~VkTextureLoader() {
    Shutdown();
}

bool VkTextureLoader::Init(VkPhysicalDevice physicalDevice, VkDevice device,
                           VkQueue queue, VkCommandPool commandPool) {
    if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE ||
        queue == VK_NULL_HANDLE || commandPool == VK_NULL_HANDLE)
        return false;
    m_physicalDevice = physicalDevice;
    m_device         = device;
    m_queue          = queue;
    m_commandPool    = commandPool;
    return true;
}

VkImageView VkTextureLoader::Load(std::string_view relativePath, bool useSrgb) {
    std::string key(relativePath);
    key += useSrgb ? ":srgb" : ":linear";
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return it->second.view;
    return LoadInternal(key, relativePath, useSrgb);
}

VkImageView VkTextureLoader::LoadCubemapHDR(std::string_view basePath) {
    std::string key(basePath);
    key += ":cubemap_hdr";
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return it->second.view;
    return LoadCubemapHDRInternal(key, basePath);
}

VkImageView VkTextureLoader::LoadCubemapHDRInternal(const std::string& key, std::string_view basePath) {
    static const char* faceNames[] = { "posx", "negx", "posy", "negy", "posz", "negz" };
    constexpr int numFaces = 6;
    int w = 0, h = 0;
    std::vector<float*> faceData(numFaces, nullptr);
    std::string base(basePath);
    if (!base.empty() && base.back() != '/' && base.back() != '\\')
        base += '/';
    for (int f = 0; f < numFaces; ++f) {
        std::string path = base + faceNames[f] + ".hdr";
        auto fileData = engine::platform::FileSystem::ReadAllBytes(path);
        if (!fileData || fileData->empty()) {
            LOG_ERROR(Render, "VkTextureLoader: failed to read cubemap face '{}'", path);
            for (int i = 0; i < f; ++i) stbi_image_free(faceData[i]);
            return VK_NULL_HANDLE;
        }
        int c = 0;
        faceData[f] = stbi_loadf_from_memory(
            fileData->data(), static_cast<int>(fileData->size()), &w, &h, &c, 4);
        if (!faceData[f] || w <= 0 || h <= 0) {
            LOG_ERROR(Render, "VkTextureLoader: stb_image HDR failed for '{}'", path);
            for (int i = 0; i <= f; ++i) stbi_image_free(faceData[i]);
            return VK_NULL_HANDLE;
        }
        if (f == 0) { (void)w; (void)h; }
    }
    const uint32_t width = static_cast<uint32_t>(w);
    const uint32_t height = static_cast<uint32_t>(h);
    const VkDeviceSize faceSize = static_cast<VkDeviceSize>(width) * height * 4 * sizeof(float);
    const VkDeviceSize stagingSize = faceSize * numFaces;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = stagingSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &stagingBuffer) != VK_SUCCESS) {
        for (int f = 0; f < numFaces; ++f) stbi_image_free(faceData[f]);
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements stagingReqs;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &stagingReqs);
    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize = stagingReqs.size;
    stagingAlloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, stagingReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        for (int f = 0; f < numFaces; ++f) stbi_image_free(faceData[f]);
        return VK_NULL_HANDLE;
    }
    void* mapped = nullptr;
    if (vkMapMemory(m_device, stagingMemory, 0, stagingSize, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        for (int f = 0; f < numFaces; ++f) stbi_image_free(faceData[f]);
        return VK_NULL_HANDLE;
    }
    for (int f = 0; f < numFaces; ++f) {
        std::memcpy(static_cast<char*>(mapped) + f * faceSize, faceData[f], faceSize);
        stbi_image_free(faceData[f]);
    }
    vkUnmapMemory(m_device, stagingMemory);

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    ici.extent = { width, height, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = numFaces;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ici, nullptr, &image) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements imageReqs;
    vkGetImageMemoryRequirements(m_device, image, &imageReqs);
    VkMemoryAllocateInfo imageAlloc{};
    imageAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAlloc.allocationSize = imageReqs.size;
    imageAlloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, imageReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imageAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_device, &imageAlloc, nullptr, &imageMemory) != VK_SUCCESS ||
        vkBindImageMemory(m_device, image, imageMemory, 0) != VK_SUCCESS) {
        vkDestroyImage(m_device, image, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return VK_NULL_HANDLE;
    }
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = m_commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &cbai, &cmd) != VK_SUCCESS) {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return VK_NULL_HANDLE;
    }
    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, numFaces };
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    std::array<VkBufferImageCopy, numFaces> regions{};
    for (int f = 0; f < numFaces; ++f) {
        regions[f].bufferOffset = f * faceSize;
        regions[f].bufferRowLength = 0;
        regions[f].bufferImageHeight = 0;
        regions[f].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(f), 1 };
        regions[f].imageOffset = { 0, 0, 0 };
        regions[f].imageExtent = { width, height, 1 };
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, numFaces, regions.data());
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    ivci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, numFaces };
    LoadedTex loaded;
    loaded.image = image;
    loaded.memory = imageMemory;
    if (vkCreateImageView(m_device, &ivci, nullptr, &loaded.view) != VK_SUCCESS) {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    auto [it, ok] = m_cache.emplace(key, loaded);
    return it->second.view;
}

VkImageView VkTextureLoader::LoadInternal(const std::string& key, std::string_view relativePath, bool useSrgb) {
    std::optional<std::vector<uint8_t>> fileData = engine::platform::FileSystem::ReadAllBytes(relativePath);
    if (!fileData || fileData->empty()) {
        LOG_ERROR(Render, "VkTextureLoader: failed to read '{}'", relativePath);
        return VK_NULL_HANDLE;
    }
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        fileData->data(), static_cast<int>(fileData->size()), &w, &h, &ch, 4);
    if (!pixels || w <= 0 || h <= 0) {
        LOG_ERROR(Render, "VkTextureLoader: stb_image failed for '{}'", relativePath);
        return VK_NULL_HANDLE;
    }
    const VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4;
    const VkFormat format = useSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = imageSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &stagingBuffer) != VK_SUCCESS) {
        stbi_image_free(pixels);
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements stagingReqs;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &stagingReqs);
    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize = stagingReqs.size;
    stagingAlloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, stagingReqs.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        stbi_image_free(pixels);
        return VK_NULL_HANDLE;
    }
    void* mapped = nullptr;
    if (vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        stbi_image_free(pixels);
        return VK_NULL_HANDLE;
    }
    std::memcpy(mapped, pixels, imageSize);
    vkUnmapMemory(m_device, stagingMemory);
    stbi_image_free(pixels);

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ici, nullptr, &image) != VK_SUCCESS) {
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements imageReqs;
    vkGetImageMemoryRequirements(m_device, image, &imageReqs);
    VkMemoryAllocateInfo imageAlloc{};
    imageAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAlloc.allocationSize = imageReqs.size;
    imageAlloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, imageReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imageAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_device, &imageAlloc, nullptr, &imageMemory) != VK_SUCCESS ||
        vkBindImageMemory(m_device, image, imageMemory, 0) != VK_SUCCESS) {
        vkDestroyImage(m_device, image, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = m_commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &cbai, &cmd) != VK_SUCCESS) {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = format;
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    LoadedTex loaded;
    loaded.image = image;
    loaded.memory = imageMemory;
    if (vkCreateImageView(m_device, &ivci, nullptr, &loaded.view) != VK_SUCCESS) {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    auto [it, ok] = m_cache.emplace(key, loaded);
    return it->second.view;
}

VkImageView VkTextureLoader::CreateDefaultTexture(uint8_t r, uint8_t g, uint8_t b, bool useSrgb) {
    return Create1x1AndCache(r, g, b, useSrgb);
}

VkImageView VkTextureLoader::CreateDefaultCubemap() {
    const std::string key = ":default_cube";
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return it->second.view;
    return CreateDefaultCubemapInternal();
}

VkImageView VkTextureLoader::CreateDefaultCubemapInternal() {
    const std::string key = ":default_cube";
    constexpr int numFaces = 6;
    const float color[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
    const VkDeviceSize faceSize = 1 * 1 * 4 * sizeof(float);
    const VkDeviceSize stagingSize = faceSize * numFaces;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = stagingSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &stagingBuffer) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    VkMemoryRequirements stagingReqs;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &stagingReqs);
    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize = stagingReqs.size;
    stagingAlloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, stagingReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    void* mapped = nullptr;
    if (vkMapMemory(m_device, stagingMemory, 0, stagingSize, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    for (int f = 0; f < numFaces; ++f)
        std::memcpy(static_cast<char*>(mapped) + f * faceSize, color, faceSize);
    vkUnmapMemory(m_device, stagingMemory);

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    ici.extent = { 1, 1, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = numFaces;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ici, nullptr, &image) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements imageReqs;
    vkGetImageMemoryRequirements(m_device, image, &imageReqs);
    VkMemoryAllocateInfo imageAlloc{};
    imageAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAlloc.allocationSize = imageReqs.size;
    imageAlloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, imageReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imageAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_device, &imageAlloc, nullptr, &imageMemory) != VK_SUCCESS ||
        vkBindImageMemory(m_device, image, imageMemory, 0) != VK_SUCCESS) {
        vkDestroyImage(m_device, image, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return VK_NULL_HANDLE;
    }
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = m_commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &cbai, &cmd) != VK_SUCCESS) {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        return VK_NULL_HANDLE;
    }
    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, numFaces };
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    std::array<VkBufferImageCopy, numFaces> regions{};
    for (int f = 0; f < numFaces; ++f) {
        regions[f].bufferOffset = f * faceSize;
        regions[f].bufferRowLength = 0;
        regions[f].bufferImageHeight = 0;
        regions[f].imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, static_cast<uint32_t>(f), 1 };
        regions[f].imageOffset = { 0, 0, 0 };
        regions[f].imageExtent = { 1, 1, 1 };
    }
    vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, numFaces, regions.data());
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);

    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    ivci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    ivci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, numFaces };
    LoadedTex loaded;
    loaded.image = image;
    loaded.memory = imageMemory;
    if (vkCreateImageView(m_device, &ivci, nullptr, &loaded.view) != VK_SUCCESS) {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    auto [it, ok] = m_cache.emplace(key, loaded);
    return it->second.view;
}

VkImageView VkTextureLoader::Create1x1AndCache(uint8_t r, uint8_t g, uint8_t b, bool useSrgb) {
    char keyBuf[64];
    snprintf(keyBuf, sizeof(keyBuf), ":def_%u_%u_%u_%s", r, g, b, useSrgb ? "s" : "l");
    std::string key(keyBuf);
    auto it = m_cache.find(key);
    if (it != m_cache.end())
        return it->second.view;
    const VkFormat format = useSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    const uint8_t pixel[4] = { r, g, b, 255 };
    const VkDeviceSize imageSize = 4;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = imageSize;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bci, nullptr, &stagingBuffer) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkMemoryRequirements stagingReqs;
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &stagingReqs);
    VkMemoryAllocateInfo stagingAlloc{};
    stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAlloc.allocationSize = stagingReqs.size;
    stagingAlloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, stagingReqs.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (stagingAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_device, &stagingAlloc, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    void* mapped = nullptr;
    if (vkMapMemory(m_device, stagingMemory, 0, imageSize, 0, &mapped) != VK_SUCCESS) {
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    std::memcpy(mapped, pixel, 4);
    vkUnmapMemory(m_device, stagingMemory);

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE;
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {1, 1, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ici, nullptr, &image) != VK_SUCCESS) {
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements imageReqs;
    vkGetImageMemoryRequirements(m_device, image, &imageReqs);
    VkMemoryAllocateInfo imageAlloc{};
    imageAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAlloc.allocationSize = imageReqs.size;
    imageAlloc.memoryTypeIndex = FindMemoryType(m_physicalDevice, imageReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imageAlloc.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(m_device, &imageAlloc, nullptr, &imageMemory) != VK_SUCCESS ||
        vkBindImageMemory(m_device, image, imageMemory, 0) != VK_SUCCESS) {
        vkDestroyImage(m_device, image, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = m_commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &cbai, &cmd) != VK_SUCCESS) {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        vkFreeMemory(m_device, stagingMemory, nullptr);
        vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        return VK_NULL_HANDLE;
    }
    VkCommandBufferBeginInfo cbbi{};
    cbbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &cbbi);
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_queue);
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &cmd);
    vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    vkFreeMemory(m_device, stagingMemory, nullptr);
    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = format;
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    LoadedTex loaded;
    loaded.image = image;
    loaded.memory = imageMemory;
    if (vkCreateImageView(m_device, &ivci, nullptr, &loaded.view) != VK_SUCCESS) {
        vkFreeMemory(m_device, imageMemory, nullptr);
        vkDestroyImage(m_device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    m_cache.emplace(key, loaded);
    return loaded.view;
}

void VkTextureLoader::DestroyTexture(LoadedTex& tex) {
    if (m_device == VK_NULL_HANDLE) return;
    if (tex.view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, tex.view, nullptr);
        tex.view = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE) {
        vkDestroyImage(m_device, tex.image, nullptr);
        tex.image = VK_NULL_HANDLE;
    }
    if (tex.memory != VK_NULL_HANDLE) {
        vkFreeMemory(m_device, tex.memory, nullptr);
        tex.memory = VK_NULL_HANDLE;
    }
}

void VkTextureLoader::Shutdown() {
    for (auto& [k, tex] : m_cache)
        DestroyTexture(tex);
    m_cache.clear();
    m_device = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_queue = VK_NULL_HANDLE;
    m_commandPool = VK_NULL_HANDLE;
}

} // namespace engine::render::vk
