#include "engine/render/terrain/HeightmapLoader.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace engine::render::terrain
{
    // ─────────────────────────────────────────────────────────────────────────────
    // HeightmapData helpers
    // ─────────────────────────────────────────────────────────────────────────────

    float HeightmapData::Sample(uint32_t x, uint32_t z) const
    {
        if (width == 0 || height == 0 || heights.empty()) return 0.0f;
        x = (x < width)  ? x : width  - 1;
        z = (z < height) ? z : height - 1;
        return static_cast<float>(heights[z * width + x]) / 65535.0f;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Internal Vulkan helpers
    // ─────────────────────────────────────────────────────────────────────────────
    namespace
    {
        uint32_t FindMemType(VkPhysicalDevice physDev,
                             uint32_t typeBits,
                             VkMemoryPropertyFlags desired)
        {
            VkPhysicalDeviceMemoryProperties props{};
            vkGetPhysicalDeviceMemoryProperties(physDev, &props);
            for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
            {
                if ((typeBits & (1u << i)) &&
                    (props.memoryTypes[i].propertyFlags & desired) == desired)
                    return i;
            }
            return UINT32_MAX;
        }

        /// Creates a host-visible, coherent staging buffer. Returns true on success.
        bool CreateStagingBuffer(VkDevice device, VkPhysicalDevice physDev,
                                 VkDeviceSize size,
                                 VkBuffer& outBuffer, VkDeviceMemory& outMemory)
        {
            VkBufferCreateInfo bi{};
            bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size        = size;
            bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bi, nullptr, &outBuffer) != VK_SUCCESS ||
                outBuffer == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkCreateBuffer (staging) failed");
                return false;
            }

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, outBuffer, &req);

            uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (memType == UINT32_MAX)
            {
                LOG_ERROR(Render, "[HeightmapLoader] No HOST_VISIBLE memory for staging buffer");
                vkDestroyBuffer(device, outBuffer, nullptr);
                outBuffer = VK_NULL_HANDLE;
                return false;
            }

            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = memType;

            if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS ||
                outMemory == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkAllocateMemory (staging) failed");
                vkDestroyBuffer(device, outBuffer, nullptr);
                outBuffer = VK_NULL_HANDLE;
                return false;
            }

            if (vkBindBufferMemory(device, outBuffer, outMemory, 0) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkBindBufferMemory (staging) failed");
                vkFreeMemory(device, outMemory, nullptr);
                vkDestroyBuffer(device, outBuffer, nullptr);
                outBuffer = VK_NULL_HANDLE;
                outMemory = VK_NULL_HANDLE;
                return false;
            }
            return true;
        }

        /// Creates a device-local image with OPTIMAL tiling. Returns true on success.
        bool CreateOptimalImage(VkDevice device, VkPhysicalDevice physDev,
                                uint32_t width, uint32_t height,
                                VkFormat format, VkImageUsageFlags usage,
                                VkImage& outImage, VkDeviceMemory& outMemory)
        {
            VkImageCreateInfo ici{};
            ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ici.imageType     = VK_IMAGE_TYPE_2D;
            ici.format        = format;
            ici.extent        = { width, height, 1 };
            ici.mipLevels     = 1;
            ici.arrayLayers   = 1;
            ici.samples       = VK_SAMPLE_COUNT_1_BIT;
            ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ici.usage         = usage;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device, &ici, nullptr, &outImage) != VK_SUCCESS ||
                outImage == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkCreateImage failed ({}x{} fmt={})",
                          width, height, static_cast<int>(format));
                return false;
            }

            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(device, outImage, &req);

            uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (memType == UINT32_MAX)
            {
                LOG_ERROR(Render, "[HeightmapLoader] No DEVICE_LOCAL memory for image");
                vkDestroyImage(device, outImage, nullptr);
                outImage = VK_NULL_HANDLE;
                return false;
            }

            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = memType;

            if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS ||
                outMemory == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkAllocateMemory (image) failed");
                vkDestroyImage(device, outImage, nullptr);
                outImage = VK_NULL_HANDLE;
                return false;
            }

            if (vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkBindImageMemory failed");
                vkFreeMemory(device, outMemory, nullptr);
                vkDestroyImage(device, outImage, nullptr);
                outImage  = VK_NULL_HANDLE;
                outMemory = VK_NULL_HANDLE;
                return false;
            }
            return true;
        }

        /// Executes a one-time command buffer: transition+copy+transition, then destroys it.
        bool UploadViaStaging(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex,
                              VkBuffer stagingBuffer, VkDeviceSize dataSize,
                              VkImage dstImage, uint32_t imgWidth, uint32_t imgHeight)
        {
            VkCommandPool pool = VK_NULL_HANDLE;
            VkCommandPoolCreateInfo poolCI{};
            poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolCI.queueFamilyIndex = queueFamilyIndex;
            poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

            if (vkCreateCommandPool(device, &poolCI, nullptr, &pool) != VK_SUCCESS ||
                pool == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkCreateCommandPool failed");
                return false;
            }

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo allocCI{};
            allocCI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocCI.commandPool        = pool;
            allocCI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocCI.commandBufferCount = 1;

            if (vkAllocateCommandBuffers(device, &allocCI, &cmd) != VK_SUCCESS ||
                cmd == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkAllocateCommandBuffers failed");
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkBeginCommandBuffer failed");
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            // Barrier: UNDEFINED → TRANSFER_DST_OPTIMAL
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image               = dstImage;
                barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                barrier.srcAccessMask       = 0;
                barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            // Copy staging buffer → image
            {
                VkBufferImageCopy region{};
                region.bufferOffset      = 0;
                region.bufferRowLength   = imgWidth;
                region.bufferImageHeight = imgHeight;
                region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                region.imageOffset       = { 0, 0, 0 };
                region.imageExtent       = { imgWidth, imgHeight, 1 };
                vkCmdCopyBufferToImage(cmd, stagingBuffer, dstImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }

            // Barrier: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image               = dstImage;
                barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkEndCommandBuffer failed");
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            VkSubmitInfo si{};
            si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers    = &cmd;

            VkFence fence = VK_NULL_HANDLE;
            VkFenceCreateInfo fenceCI{};
            fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            if (vkCreateFence(device, &fenceCI, nullptr, &fence) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkCreateFence failed");
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            if (vkQueueSubmit(queue, 1, &si, fence) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[HeightmapLoader] vkQueueSubmit failed");
                vkDestroyFence(device, fence, nullptr);
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device, fence, nullptr);
            vkDestroyCommandPool(device, pool, nullptr);

            (void)dataSize;
            return true;
        }
    } // namespace

    // ─────────────────────────────────────────────────────────────────────────────
    // HeightmapLoader::LoadFromFile
    // ─────────────────────────────────────────────────────────────────────────────

    bool HeightmapLoader::LoadFromFile(const std::string& fullPath, HeightmapData& outData)
    {
        LOG_INFO(Render, "[HeightmapLoader] Loading '{}'", fullPath);

        std::ifstream file(fullPath, std::ios::binary);
        if (!file.is_open())
        {
            LOG_WARN(Render, "[HeightmapLoader] File not found: '{}'", fullPath);
            return false;
        }

        // Header: magic(4) + width(4) + height(4)
        uint32_t magic = 0, width = 0, height = 0;
        file.read(reinterpret_cast<char*>(&magic),  sizeof(magic));
        file.read(reinterpret_cast<char*>(&width),  sizeof(width));
        file.read(reinterpret_cast<char*>(&height), sizeof(height));

        if (!file || magic != kHeightmapMagic || width == 0 || height == 0)
        {
            LOG_ERROR(Render,
                "[HeightmapLoader] Invalid header (magic=0x{:08X} w={} h={})",
                magic, width, height);
            return false;
        }

        const size_t pixelCount = static_cast<size_t>(width) * height;
        outData.width  = width;
        outData.height = height;
        outData.heights.resize(pixelCount);

        file.read(reinterpret_cast<char*>(outData.heights.data()),
                  static_cast<std::streamsize>(pixelCount * sizeof(uint16_t)));

        if (!file)
        {
            LOG_ERROR(Render, "[HeightmapLoader] Truncated file: '{}'", fullPath);
            outData.heights.clear();
            return false;
        }

        LOG_INFO(Render, "[HeightmapLoader] Loaded OK ({}x{}, {} pixels)", width, height, pixelCount);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // HeightmapLoader::UploadHeightmap
    // ─────────────────────────────────────────────────────────────────────────────

    bool HeightmapLoader::UploadHeightmap(VkDevice device, VkPhysicalDevice physDev,
                                          const HeightmapData& data,
                                          VkQueue queue, uint32_t queueFamilyIndex,
                                          HeightmapGpu& outGpu)
    {
        LOG_INFO(Render, "[HeightmapLoader] UploadHeightmap {}x{}", data.width, data.height);

        if (device == VK_NULL_HANDLE || physDev == VK_NULL_HANDLE)
        {
            LOG_ERROR(Render, "[HeightmapLoader] UploadHeightmap: invalid device");
            return false;
        }
        if (data.heights.empty())
        {
            LOG_ERROR(Render, "[HeightmapLoader] UploadHeightmap: no data to upload");
            return false;
        }

        const VkDeviceSize dataBytes =
            static_cast<VkDeviceSize>(data.width) * data.height * sizeof(uint16_t);

        // Create staging buffer
        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!CreateStagingBuffer(device, physDev, dataBytes, stagingBuf, stagingMem))
        {
            LOG_ERROR(Render, "[HeightmapLoader] Failed to create staging buffer");
            return false;
        }

        // Copy pixel data into staging
        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMem, 0, dataBytes, 0, &mapped) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[HeightmapLoader] vkMapMemory (staging) failed");
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }
        std::memcpy(mapped, data.heights.data(), static_cast<size_t>(dataBytes));
        vkUnmapMemory(device, stagingMem);

        // Create DEVICE_LOCAL R16_UNORM image
        if (!CreateOptimalImage(device, physDev,
                                data.width, data.height,
                                VK_FORMAT_R16_UNORM,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                outGpu.image, outGpu.memory))
        {
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }

        // Upload via staging
        if (!UploadViaStaging(device, queue, queueFamilyIndex,
                              stagingBuf, dataBytes,
                              outGpu.image, data.width, data.height))
        {
            LOG_ERROR(Render, "[HeightmapLoader] Staging upload failed");
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            DestroyHeightmap(device, outGpu);
            return false;
        }

        // Clean up staging
        vkDestroyBuffer(device, stagingBuf, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);

        // Image view
        VkImageViewCreateInfo viewCI{};
        viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image                           = outGpu.image;
        viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format                          = VK_FORMAT_R16_UNORM;
        viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.baseMipLevel   = 0;
        viewCI.subresourceRange.levelCount     = 1;
        viewCI.subresourceRange.baseArrayLayer = 0;
        viewCI.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &viewCI, nullptr, &outGpu.view) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[HeightmapLoader] vkCreateImageView failed");
            DestroyHeightmap(device, outGpu);
            return false;
        }

        // Sampler (linear, clamp-to-edge for heightmap boundaries)
        VkSamplerCreateInfo sampCI{};
        sampCI.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampCI.magFilter    = VK_FILTER_LINEAR;
        sampCI.minFilter    = VK_FILTER_LINEAR;
        sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.maxLod       = 0.0f;

        if (vkCreateSampler(device, &sampCI, nullptr, &outGpu.sampler) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[HeightmapLoader] vkCreateSampler failed");
            DestroyHeightmap(device, outGpu);
            return false;
        }

        outGpu.width  = data.width;
        outGpu.height = data.height;
        LOG_INFO(Render, "[HeightmapLoader] UploadHeightmap OK ({}x{})", data.width, data.height);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // HeightmapLoader::GenerateAndUploadNormalMap
    // ─────────────────────────────────────────────────────────────────────────────

    bool HeightmapLoader::GenerateAndUploadNormalMap(VkDevice device, VkPhysicalDevice physDev,
                                                     const HeightmapData& data,
                                                     float heightScale, float worldScale,
                                                     VkQueue queue, uint32_t queueFamilyIndex,
                                                     NormalMapGpu& outNormal)
    {
        LOG_INFO(Render, "[HeightmapLoader] GenerateNormalMap {}x{}", data.width, data.height);

        if (data.heights.empty() || data.width < 3 || data.height < 3)
        {
            LOG_ERROR(Render, "[HeightmapLoader] Heightmap too small for normal generation");
            return false;
        }

        const uint32_t w = data.width;
        const uint32_t h = data.height;
        const float    invScale = heightScale / worldScale; // ratio of vertical to horizontal

        // Generate RGBA8 normal map using Sobel filter
        std::vector<uint8_t> normalPixels(static_cast<size_t>(w) * h * 4);

        auto sampleH = [&](int x, int z) -> float {
            x = (x < 0) ? 0 : ((x >= (int)w) ? (int)w - 1 : x);
            z = (z < 0) ? 0 : ((z >= (int)h) ? (int)h - 1 : z);
            return static_cast<float>(data.heights[static_cast<size_t>(z) * w + x]) / 65535.0f;
        };

        for (uint32_t zi = 0; zi < h; ++zi)
        {
            for (uint32_t xi = 0; xi < w; ++xi)
            {
                int ix = static_cast<int>(xi);
                int iz = static_cast<int>(zi);

                // Sobel X gradient (horizontal)
                float gx = (sampleH(ix + 1, iz - 1) + 2.0f * sampleH(ix + 1, iz) + sampleH(ix + 1, iz + 1))
                          - (sampleH(ix - 1, iz - 1) + 2.0f * sampleH(ix - 1, iz) + sampleH(ix - 1, iz + 1));
                // Sobel Z gradient (depth)
                float gz = (sampleH(ix - 1, iz + 1) + 2.0f * sampleH(ix, iz + 1) + sampleH(ix + 1, iz + 1))
                          - (sampleH(ix - 1, iz - 1) + 2.0f * sampleH(ix, iz - 1) + sampleH(ix + 1, iz - 1));

                // Scale by height-to-world ratio and negate for correct facing
                gx *= invScale;
                gz *= invScale;

                // Normal = normalize(-Gx, 1.0, -Gz)  (Y-up, standard terrain convention)
                float nx = -gx;
                float ny = 1.0f;
                float nz = -gz;
                float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }
                else             { nx = 0.0f; ny = 1.0f; nz = 0.0f; }

                // Encode as RGBA8: N * 0.5 + 0.5, A=255
                const size_t idx = (static_cast<size_t>(zi) * w + xi) * 4;
                normalPixels[idx + 0] = static_cast<uint8_t>((nx * 0.5f + 0.5f) * 255.0f);
                normalPixels[idx + 1] = static_cast<uint8_t>((ny * 0.5f + 0.5f) * 255.0f);
                normalPixels[idx + 2] = static_cast<uint8_t>((nz * 0.5f + 0.5f) * 255.0f);
                normalPixels[idx + 3] = 255u;
            }
        }

        const VkDeviceSize dataBytes = static_cast<VkDeviceSize>(w) * h * 4;

        // Staging buffer
        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!CreateStagingBuffer(device, physDev, dataBytes, stagingBuf, stagingMem))
        {
            LOG_ERROR(Render, "[HeightmapLoader] Failed to create normal map staging buffer");
            return false;
        }

        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMem, 0, dataBytes, 0, &mapped) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[HeightmapLoader] vkMapMemory (normal staging) failed");
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }
        std::memcpy(mapped, normalPixels.data(), static_cast<size_t>(dataBytes));
        vkUnmapMemory(device, stagingMem);

        // Create DEVICE_LOCAL RGBA8_UNORM image
        if (!CreateOptimalImage(device, physDev, w, h,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                outNormal.image, outNormal.memory))
        {
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }

        if (!UploadViaStaging(device, queue, queueFamilyIndex,
                              stagingBuf, dataBytes, outNormal.image, w, h))
        {
            LOG_ERROR(Render, "[HeightmapLoader] Normal map staging upload failed");
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            DestroyNormalMap(device, outNormal);
            return false;
        }

        vkDestroyBuffer(device, stagingBuf, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);

        // Image view
        VkImageViewCreateInfo viewCI{};
        viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image                           = outNormal.image;
        viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format                          = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.baseMipLevel   = 0;
        viewCI.subresourceRange.levelCount     = 1;
        viewCI.subresourceRange.baseArrayLayer = 0;
        viewCI.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &viewCI, nullptr, &outNormal.view) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[HeightmapLoader] vkCreateImageView (normal) failed");
            DestroyNormalMap(device, outNormal);
            return false;
        }

        // Sampler (bilinear, clamp-to-edge)
        VkSamplerCreateInfo sampCI{};
        sampCI.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampCI.magFilter    = VK_FILTER_LINEAR;
        sampCI.minFilter    = VK_FILTER_LINEAR;
        sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.maxLod       = 0.0f;

        if (vkCreateSampler(device, &sampCI, nullptr, &outNormal.sampler) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[HeightmapLoader] vkCreateSampler (normal) failed");
            DestroyNormalMap(device, outNormal);
            return false;
        }

        LOG_INFO(Render, "[HeightmapLoader] Normal map GPU upload OK ({}x{})", w, h);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Destroy helpers
    // ─────────────────────────────────────────────────────────────────────────────

    void HeightmapLoader::DestroyHeightmap(VkDevice device, HeightmapGpu& gpu)
    {
        if (gpu.sampler != VK_NULL_HANDLE) { vkDestroySampler(device, gpu.sampler, nullptr); gpu.sampler = VK_NULL_HANDLE; }
        if (gpu.view    != VK_NULL_HANDLE) { vkDestroyImageView(device, gpu.view, nullptr);  gpu.view    = VK_NULL_HANDLE; }
        if (gpu.image   != VK_NULL_HANDLE) { vkDestroyImage(device, gpu.image, nullptr);     gpu.image   = VK_NULL_HANDLE; }
        if (gpu.memory  != VK_NULL_HANDLE) { vkFreeMemory(device, gpu.memory, nullptr);      gpu.memory  = VK_NULL_HANDLE; }
        gpu.width = gpu.height = 0;
        LOG_INFO(Render, "[HeightmapLoader] HeightmapGpu destroyed");
    }

    void HeightmapLoader::DestroyNormalMap(VkDevice device, NormalMapGpu& nm)
    {
        if (nm.sampler != VK_NULL_HANDLE) { vkDestroySampler(device, nm.sampler, nullptr); nm.sampler = VK_NULL_HANDLE; }
        if (nm.view    != VK_NULL_HANDLE) { vkDestroyImageView(device, nm.view, nullptr);  nm.view    = VK_NULL_HANDLE; }
        if (nm.image   != VK_NULL_HANDLE) { vkDestroyImage(device, nm.image, nullptr);     nm.image   = VK_NULL_HANDLE; }
        if (nm.memory  != VK_NULL_HANDLE) { vkFreeMemory(device, nm.memory, nullptr);      nm.memory  = VK_NULL_HANDLE; }
        LOG_INFO(Render, "[HeightmapLoader] NormalMapGpu destroyed");
    }

} // namespace engine::render::terrain
