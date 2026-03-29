#include "engine/render/terrain/TerrainHoleMask.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cstring>
#include <fstream>
#include <functional>

namespace engine::render::terrain
{
    // ─────────────────────────────────────────────────────────────────────────
    // HoleMaskData
    // ─────────────────────────────────────────────────────────────────────────

    bool HoleMaskData::IsHole(uint32_t qx, uint32_t qz) const
    {
        if (qx >= width || qz >= height) return false;
        return mask[qz * width + qx] == 0u;
    }

    bool HoleMaskData::HasAnyHole() const
    {
        for (uint8_t v : mask)
            if (v == 0u) return true;
        return false;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Internal helpers
    // ─────────────────────────────────────────────────────────────────────────
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

        /// Submits a one-time command buffer: begin → record(fn) → end → submit → wait.
        bool OneTimeSubmit(VkDevice device, VkQueue queue,
                           uint32_t queueFamilyIndex,
                           const std::function<void(VkCommandBuffer)>& record)
        {
            VkCommandPoolCreateInfo poolCI{};
            poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            poolCI.queueFamilyIndex = queueFamilyIndex;

            VkCommandPool pool = VK_NULL_HANDLE;
            if (vkCreateCommandPool(device, &poolCI, nullptr, &pool) != VK_SUCCESS)
                return false;

            VkCommandBufferAllocateInfo cbAI{};
            cbAI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cbAI.commandPool        = pool;
            cbAI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cbAI.commandBufferCount = 1;

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            if (vkAllocateCommandBuffers(device, &cbAI, &cmd) != VK_SUCCESS)
            {
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &beginInfo);
            record(cmd);
            vkEndCommandBuffer(cmd);

            VkSubmitInfo si{};
            si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers    = &cmd;
            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);

            vkDestroyCommandPool(device, pool, nullptr);
            return true;
        }
    } // namespace

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainHoleMask::LoadFromFile
    // ─────────────────────────────────────────────────────────────────────────

    bool TerrainHoleMask::LoadFromFile(const std::string& fullPath, HoleMaskData& outData)
    {
        std::ifstream fs(fullPath, std::ios::binary);
        if (!fs.is_open())
        {
            LOG_WARN(Render, "[TerrainHoleMask] File not found: '{}'", fullPath);
            return false;
        }

        uint32_t magic  = 0u;
        uint32_t width  = 0u;
        uint32_t height = 0u;
        fs.read(reinterpret_cast<char*>(&magic),  sizeof(magic));
        fs.read(reinterpret_cast<char*>(&width),  sizeof(width));
        fs.read(reinterpret_cast<char*>(&height), sizeof(height));

        if (!fs || magic != kHoleMaskMagic)
        {
            LOG_WARN(Render, "[TerrainHoleMask] Invalid magic in '{}' (got 0x{:08X}, expected 0x{:08X})",
                     fullPath, magic, kHoleMaskMagic);
            return false;
        }

        if (width == 0u || height == 0u)
        {
            LOG_WARN(Render, "[TerrainHoleMask] Zero-size mask in '{}'", fullPath);
            return false;
        }

        const size_t dataSize = static_cast<size_t>(width) * height;
        outData.mask.resize(dataSize);
        fs.read(reinterpret_cast<char*>(outData.mask.data()), static_cast<std::streamsize>(dataSize));

        if (!fs)
        {
            LOG_WARN(Render, "[TerrainHoleMask] Truncated data in '{}'", fullPath);
            outData.mask.clear();
            return false;
        }

        outData.width  = width;
        outData.height = height;

        const bool hasHoles = outData.HasAnyHole();
        LOG_INFO(Render, "[TerrainHoleMask] Loaded '{}' ({}×{} quads, holes={})",
                 fullPath, width, height, hasHoles ? "yes" : "no");
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainHoleMask::GenerateSolid
    // ─────────────────────────────────────────────────────────────────────────

    void TerrainHoleMask::GenerateSolid(uint32_t quadW, uint32_t quadH, HoleMaskData& outData)
    {
        outData.width  = quadW;
        outData.height = quadH;
        outData.mask.assign(static_cast<size_t>(quadW) * quadH, 255u);
        LOG_DEBUG(Render, "[TerrainHoleMask] Generated solid fallback mask ({}×{} quads)", quadW, quadH);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainHoleMask::UploadToGpu
    // ─────────────────────────────────────────────────────────────────────────

    bool TerrainHoleMask::UploadToGpu(VkDevice device, VkPhysicalDevice physDev,
                                      const HoleMaskData& data,
                                      VkQueue queue, uint32_t queueFamilyIndex,
                                      HoleMaskGpu& outGpu)
    {
        if (data.width == 0u || data.height == 0u || data.mask.empty())
        {
            LOG_ERROR(Render, "[TerrainHoleMask] UploadToGpu: empty data");
            return false;
        }

        const VkDeviceSize dataBytes = static_cast<VkDeviceSize>(data.width) * data.height;

        // ── Staging buffer (HOST_VISIBLE | HOST_COHERENT) ─────────────────────
        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        {
            VkBufferCreateInfo bci{};
            bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bci.size        = dataBytes;
            bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bci, nullptr, &stagingBuf) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainHoleMask] vkCreateBuffer (staging) failed");
                return false;
            }

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, stagingBuf, &req);

            const uint32_t mt = FindMemType(physDev, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (mt == UINT32_MAX)
            {
                LOG_ERROR(Render, "[TerrainHoleMask] No HOST_VISIBLE memory for staging");
                vkDestroyBuffer(device, stagingBuf, nullptr);
                return false;
            }

            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = mt;

            if (vkAllocateMemory(device, &ai, nullptr, &stagingMem) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainHoleMask] vkAllocateMemory (staging) failed");
                vkDestroyBuffer(device, stagingBuf, nullptr);
                return false;
            }
            vkBindBufferMemory(device, stagingBuf, stagingMem, 0);

            void* mapped = nullptr;
            vkMapMemory(device, stagingMem, 0, dataBytes, 0, &mapped);
            std::memcpy(mapped, data.mask.data(), static_cast<size_t>(dataBytes));
            vkUnmapMemory(device, stagingMem);
        }

        // ── Device-local R8_UNORM image ──────────────────────────────────────
        {
            VkImageCreateInfo ici{};
            ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ici.imageType     = VK_IMAGE_TYPE_2D;
            ici.format        = VK_FORMAT_R8_UNORM;
            ici.extent        = { data.width, data.height, 1u };
            ici.mipLevels     = 1u;
            ici.arrayLayers   = 1u;
            ici.samples       = VK_SAMPLE_COUNT_1_BIT;
            ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ici.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            if (vkCreateImage(device, &ici, nullptr, &outGpu.image) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainHoleMask] vkCreateImage failed");
                vkDestroyBuffer(device, stagingBuf, nullptr);
                vkFreeMemory(device, stagingMem, nullptr);
                return false;
            }

            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(device, outGpu.image, &req);

            const uint32_t mt = FindMemType(physDev, req.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (mt == UINT32_MAX)
            {
                LOG_ERROR(Render, "[TerrainHoleMask] No DEVICE_LOCAL memory for image");
                vkDestroyImage(device, outGpu.image, nullptr);
                outGpu.image = VK_NULL_HANDLE;
                vkDestroyBuffer(device, stagingBuf, nullptr);
                vkFreeMemory(device, stagingMem, nullptr);
                return false;
            }

            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = mt;

            if (vkAllocateMemory(device, &ai, nullptr, &outGpu.memory) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainHoleMask] vkAllocateMemory (image) failed");
                vkDestroyImage(device, outGpu.image, nullptr);
                outGpu.image = VK_NULL_HANDLE;
                vkDestroyBuffer(device, stagingBuf, nullptr);
                vkFreeMemory(device, stagingMem, nullptr);
                return false;
            }
            vkBindImageMemory(device, outGpu.image, outGpu.memory, 0);
        }

        // ── Upload via staging (UNDEFINED → TRANSFER_DST → SHADER_READ_ONLY) ─
        const bool uploaded = OneTimeSubmit(device, queue, queueFamilyIndex,
            [&](VkCommandBuffer cmd)
            {
                // Transition: UNDEFINED → TRANSFER_DST_OPTIMAL
                VkImageMemoryBarrier barrier{};
                barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
                barrier.image                           = outGpu.image;
                barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel   = 0;
                barrier.subresourceRange.levelCount     = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount     = 1;
                barrier.srcAccessMask                   = 0;
                barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

                // Copy staging buffer → image
                VkBufferImageCopy region{};
                region.bufferOffset                    = 0;
                region.bufferRowLength                 = 0;
                region.bufferImageHeight               = 0;
                region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel       = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount     = 1;
                region.imageOffset                     = { 0, 0, 0 };
                region.imageExtent                     = { data.width, data.height, 1u };
                vkCmdCopyBufferToImage(cmd, stagingBuf, outGpu.image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                // Transition: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
                barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            });

        vkDestroyBuffer(device, stagingBuf, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);

        if (!uploaded)
        {
            LOG_ERROR(Render, "[TerrainHoleMask] OneTimeSubmit upload failed");
            DestroyGpu(device, outGpu);
            return false;
        }

        // ── Image view ────────────────────────────────────────────────────────
        {
            VkImageViewCreateInfo vci{};
            vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vci.image                           = outGpu.image;
            vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            vci.format                          = VK_FORMAT_R8_UNORM;
            vci.components.r                    = VK_COMPONENT_SWIZZLE_R;
            vci.components.g                    = VK_COMPONENT_SWIZZLE_ZERO;
            vci.components.b                    = VK_COMPONENT_SWIZZLE_ZERO;
            vci.components.a                    = VK_COMPONENT_SWIZZLE_ONE;
            vci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            vci.subresourceRange.baseMipLevel   = 0;
            vci.subresourceRange.levelCount     = 1;
            vci.subresourceRange.baseArrayLayer = 0;
            vci.subresourceRange.layerCount     = 1;

            if (vkCreateImageView(device, &vci, nullptr, &outGpu.view) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainHoleMask] vkCreateImageView failed");
                DestroyGpu(device, outGpu);
                return false;
            }
        }

        // ── Sampler (NEAREST — one texel per quad, no interpolation) ─────────
        {
            VkSamplerCreateInfo sci{};
            sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sci.magFilter    = VK_FILTER_NEAREST;
            sci.minFilter    = VK_FILTER_NEAREST;
            sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.minLod       = 0.0f;
            sci.maxLod       = 0.0f;

            if (vkCreateSampler(device, &sci, nullptr, &outGpu.sampler) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainHoleMask] vkCreateSampler failed");
                DestroyGpu(device, outGpu);
                return false;
            }
        }

        outGpu.width  = data.width;
        outGpu.height = data.height;

        LOG_INFO(Render, "[TerrainHoleMask] GPU upload OK ({}×{} quads, R8_UNORM)",
                 data.width, data.height);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainHoleMask::DestroyGpu
    // ─────────────────────────────────────────────────────────────────────────

    void TerrainHoleMask::DestroyGpu(VkDevice device, HoleMaskGpu& gpu)
    {
        if (gpu.sampler != VK_NULL_HANDLE) { vkDestroySampler(device, gpu.sampler, nullptr);   gpu.sampler = VK_NULL_HANDLE; }
        if (gpu.view    != VK_NULL_HANDLE) { vkDestroyImageView(device, gpu.view, nullptr);    gpu.view    = VK_NULL_HANDLE; }
        if (gpu.image   != VK_NULL_HANDLE) { vkDestroyImage(device, gpu.image, nullptr);       gpu.image   = VK_NULL_HANDLE; }
        if (gpu.memory  != VK_NULL_HANDLE) { vkFreeMemory(device, gpu.memory, nullptr);        gpu.memory  = VK_NULL_HANDLE; }
        gpu.width  = 0u;
        gpu.height = 0u;
        LOG_INFO(Render, "[TerrainHoleMask] GPU resources destroyed");
    }

} // namespace engine::render::terrain
