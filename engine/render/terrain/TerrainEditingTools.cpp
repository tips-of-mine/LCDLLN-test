#include "engine/render/terrain/TerrainEditingTools.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace engine::render::terrain
{
    void RecordHeightmapR16UploadCommands(VkCommandBuffer cmd,
                                          VkBuffer stagingBuffer,
                                          VkDeviceSize stagingBufferOffset,
                                          VkImage dstImage,
                                          uint32_t width,
                                          uint32_t height)
    {
        if (cmd == VK_NULL_HANDLE || stagingBuffer == VK_NULL_HANDLE || dstImage == VK_NULL_HANDLE || width == 0u
            || height == 0u)
        {
            return;
        }

        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = dstImage;
        toTransfer.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransfer);

        VkBufferImageCopy region{};
        region.bufferOffset = stagingBufferOffset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { width, height, 1 };
        vkCmdCopyBufferToImage(cmd, stagingBuffer, dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toSample{};
        toSample.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toSample.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSample.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toSample.image = dstImage;
        toSample.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toSample.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toSample.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toSample);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Internal Vulkan helpers (raw, no VMA)
    // ─────────────────────────────────────────────────────────────────────────────
    namespace
    {
        /// Finds a memory type index matching the desired property flags.
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

        /// Creates a HOST_VISIBLE | HOST_COHERENT staging buffer of `size` bytes.
        bool CreateStagingBuffer(VkDevice device, VkPhysicalDevice physDev,
                                 VkDeviceSize size,
                                 VkBuffer& outBuf, VkDeviceMemory& outMem)
        {
            VkBufferCreateInfo bi{};
            bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size        = size;
            bi.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bi, nullptr, &outBuf) != VK_SUCCESS ||
                outBuf == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainEditingTools] vkCreateBuffer (staging) failed");
                return false;
            }

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, outBuf, &req);

            const uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (memType == UINT32_MAX)
            {
                LOG_ERROR(Render, "[TerrainEditingTools] No HOST_VISIBLE memory for staging");
                vkDestroyBuffer(device, outBuf, nullptr);
                outBuf = VK_NULL_HANDLE;
                return false;
            }

            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = memType;

            if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS ||
                outMem == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainEditingTools] vkAllocateMemory (staging) failed");
                vkDestroyBuffer(device, outBuf, nullptr);
                outBuf = VK_NULL_HANDLE;
                return false;
            }

            if (vkBindBufferMemory(device, outBuf, outMem, 0) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainEditingTools] vkBindBufferMemory (staging) failed");
                vkFreeMemory(device, outMem, nullptr);
                vkDestroyBuffer(device, outBuf, nullptr);
                outBuf = VK_NULL_HANDLE;
                outMem = VK_NULL_HANDLE;
                return false;
            }
            return true;
        }

        /// Submits a one-time command that:
        ///   SHADER_READ_ONLY → TRANSFER_DST → copies staging→image → SHADER_READ_ONLY.
        /// `format` must be R16_UNORM (heightmap) or B8G8R8A8/R8G8B8A8_UNORM (splatmap).
        bool UploadToImage(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex,
                           VkBuffer stagingBuf,
                           VkImage dstImage,
                           uint32_t width, uint32_t height)
        {
            VkCommandPool pool = VK_NULL_HANDLE;
            {
                VkCommandPoolCreateInfo poolCI{};
                poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                poolCI.queueFamilyIndex = queueFamilyIndex;
                poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
                if (vkCreateCommandPool(device, &poolCI, nullptr, &pool) != VK_SUCCESS ||
                    pool == VK_NULL_HANDLE)
                {
                    LOG_ERROR(Render, "[TerrainEditingTools] vkCreateCommandPool failed");
                    return false;
                }
            }

            VkCommandBuffer cmd = VK_NULL_HANDLE;
            {
                VkCommandBufferAllocateInfo allocCI{};
                allocCI.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocCI.commandPool        = pool;
                allocCI.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocCI.commandBufferCount = 1;
                if (vkAllocateCommandBuffers(device, &allocCI, &cmd) != VK_SUCCESS ||
                    cmd == VK_NULL_HANDLE)
                {
                    LOG_ERROR(Render, "[TerrainEditingTools] vkAllocateCommandBuffers failed");
                    vkDestroyCommandPool(device, pool, nullptr);
                    return false;
                }
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainEditingTools] vkBeginCommandBuffer failed");
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            // Barrier: SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL
            {
                VkImageMemoryBarrier b{};
                b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image               = dstImage;
                b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                b.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &b);
            }

            // Copy staging buffer → image
            {
                VkBufferImageCopy region{};
                region.bufferOffset                    = 0;
                region.bufferRowLength                 = 0;
                region.bufferImageHeight               = 0;
                region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel       = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount     = 1;
                region.imageOffset                     = { 0, 0, 0 };
                region.imageExtent                     = { width, height, 1 };
                vkCmdCopyBufferToImage(cmd, stagingBuf, dstImage,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            }

            // Barrier: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
            {
                VkImageMemoryBarrier b{};
                b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                b.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                b.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image               = dstImage;
                b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                b.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                b.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &b);
            }

            vkEndCommandBuffer(cmd);

            VkSubmitInfo si{};
            si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount   = 1;
            si.pCommandBuffers      = &cmd;
            vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(queue);

            vkDestroyCommandPool(device, pool, nullptr);
            return true;
        }

    } // anonymous namespace

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainEditingTools::Init / Shutdown
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainEditingTools::Init(HeightmapData*    heightmap,
                                   TerrainSplatting* splatting,
                                   float terrainOriginX,
                                   float terrainOriginZ,
                                   float terrainWorldSize,
                                   float heightScale)
    {
        if (!heightmap || !splatting)
        {
            LOG_ERROR(Render, "[TerrainEditingTools] Init: null heightmap or splatting pointer");
            return false;
        }
        if (terrainWorldSize <= 0.0f)
        {
            LOG_ERROR(Render, "[TerrainEditingTools] Init: terrainWorldSize must be > 0 (got {})",
                      terrainWorldSize);
            return false;
        }
        if (!splatting->HasCpuData())
        {
            LOG_ERROR(Render, "[TerrainEditingTools] Init: splatting has no CPU data — "
                              "ensure TerrainSplatting was Init'd before TerrainEditingTools");
            return false;
        }

        m_heightmap        = heightmap;
        m_splatting        = splatting;
        m_terrainOriginX   = terrainOriginX;
        m_terrainOriginZ   = terrainOriginZ;
        m_terrainWorldSize = terrainWorldSize;
        m_heightScale      = heightScale;
        m_dirtyHeightmap   = false;
        m_dirtySplatMap    = false;
        m_initialized      = true;

        LOG_INFO(Render,
                 "[TerrainEditingTools] Init OK (origin=({},{}) size={} hscale={} "
                 "hmap={}x{} splat={}x{})",
                 terrainOriginX, terrainOriginZ,
                 terrainWorldSize, heightScale,
                 heightmap->width, heightmap->height,
                 splatting->GetSplatMapCpuWidth(),
                 splatting->GetSplatMapCpuHeight());
        return true;
    }

    void TerrainEditingTools::Shutdown()
    {
        m_heightmap      = nullptr;
        m_splatting      = nullptr;
        m_initialized    = false;
        m_dirtyHeightmap = false;
        m_dirtySplatMap  = false;
        LOG_INFO(Render, "[TerrainEditingTools] Shutdown");
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // Internal coordinate helpers
    // ─────────────────────────────────────────────────────────────────────────────

    float TerrainEditingTools::ComputeKernel(float dist,
                                              float radius,
                                              float falloff) const
    {
        if (dist >= radius) return 0.0f;
        const float t = 1.0f - (dist / radius); // [0, 1]
        return std::pow(t, std::max(falloff, 0.01f));
    }

    void TerrainEditingTools::WorldToHeightmapPixel(float worldX, float worldZ,
                                                     int32_t& outPx, int32_t& outPz) const
    {
        // Terrain spans [originX, originX + worldSize] × [originZ, originZ + worldSize].
        // Heightmap pixel (0,0) is the corner at (originX, originZ).
        const float relX = worldX - m_terrainOriginX;
        const float relZ = worldZ - m_terrainOriginZ;
        const float normX = relX / m_terrainWorldSize;
        const float normZ = relZ / m_terrainWorldSize;
        outPx = static_cast<int32_t>(normX * static_cast<float>(m_heightmap->width));
        outPz = static_cast<int32_t>(normZ * static_cast<float>(m_heightmap->height));
    }

    void TerrainEditingTools::WorldToSplatPixel(float worldX, float worldZ,
                                                 int32_t& outPx, int32_t& outPz) const
    {
        const float relX  = worldX - m_terrainOriginX;
        const float relZ  = worldZ - m_terrainOriginZ;
        const float normX = relX / m_terrainWorldSize;
        const float normZ = relZ / m_terrainWorldSize;
        outPx = static_cast<int32_t>(normX * static_cast<float>(m_splatting->GetSplatMapCpuWidth()));
        outPz = static_cast<int32_t>(normZ * static_cast<float>(m_splatting->GetSplatMapCpuHeight()));
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainEditingTools::ApplyBrush
    // ─────────────────────────────────────────────────────────────────────────────

    void TerrainEditingTools::ApplyBrush(float worldX, float worldZ,
                                          BrushOp op, const BrushParams& params)
    {
        if (!m_initialized || !m_heightmap || m_heightmap->heights.empty())
        {
            LOG_WARN(Render, "[TerrainEditingTools] ApplyBrush called on uninitialised tools");
            return;
        }

        const uint32_t w = m_heightmap->width;
        const uint32_t h = m_heightmap->height;

        // World units per heightmap texel
        const float worldStep = m_terrainWorldSize / static_cast<float>(w);

        // Brush centre in heightmap pixel space
        int32_t cx = 0, cz = 0;
        WorldToHeightmapPixel(worldX, worldZ, cx, cz);

        // Pixel radius (add 1 to cover partial texels at the boundary)
        const int32_t iRadius = static_cast<int32_t>(params.radius / worldStep) + 1;

        if (op == BrushOp::Smooth)
        {
            // Smooth needs a read-only snapshot to avoid propagating within one pass.
            const std::vector<uint16_t> snapshot = m_heightmap->heights;

            for (int32_t iz = cz - iRadius; iz <= cz + iRadius; ++iz)
            {
                for (int32_t ix = cx - iRadius; ix <= cx + iRadius; ++ix)
                {
                    if (ix < 0 || ix >= static_cast<int32_t>(w) ||
                        iz < 0 || iz >= static_cast<int32_t>(h))
                        continue;

                    const float dx   = static_cast<float>(ix - cx) * worldStep;
                    const float dz   = static_cast<float>(iz - cz) * worldStep;
                    const float dist = std::sqrt(dx * dx + dz * dz);
                    const float k    = ComputeKernel(dist, params.radius, params.falloff);
                    if (k <= 0.0f) continue;

                    // 3×3 neighbourhood average from snapshot
                    float sum   = 0.0f;
                    int   count = 0;
                    for (int32_t nz = iz - 1; nz <= iz + 1; ++nz)
                    {
                        for (int32_t nx = ix - 1; nx <= ix + 1; ++nx)
                        {
                            const int32_t snx = std::clamp(nx, 0, static_cast<int32_t>(w) - 1);
                            const int32_t snz = std::clamp(nz, 0, static_cast<int32_t>(h) - 1);
                            sum   += static_cast<float>(snapshot[static_cast<uint32_t>(snz) * w +
                                                                  static_cast<uint32_t>(snx)]);
                            ++count;
                        }
                    }
                    const float avg    = sum / static_cast<float>(count);
                    const float orig   = static_cast<float>(
                        m_heightmap->heights[static_cast<uint32_t>(iz) * w +
                                             static_cast<uint32_t>(ix)]);
                    const float result = orig + (avg - orig) * k * params.strength;
                    m_heightmap->heights[static_cast<uint32_t>(iz) * w +
                                         static_cast<uint32_t>(ix)] =
                        static_cast<uint16_t>(std::clamp(result, 0.0f, 65535.0f));
                }
            }
        }
        else
        {
            for (int32_t iz = cz - iRadius; iz <= cz + iRadius; ++iz)
            {
                for (int32_t ix = cx - iRadius; ix <= cx + iRadius; ++ix)
                {
                    if (ix < 0 || ix >= static_cast<int32_t>(w) ||
                        iz < 0 || iz >= static_cast<int32_t>(h))
                        continue;

                    const float dx   = static_cast<float>(ix - cx) * worldStep;
                    const float dz   = static_cast<float>(iz - cz) * worldStep;
                    const float dist = std::sqrt(dx * dx + dz * dz);
                    const float k    = ComputeKernel(dist, params.radius, params.falloff);
                    if (k <= 0.0f) continue;

                    const uint32_t idx = static_cast<uint32_t>(iz) * w +
                                         static_cast<uint32_t>(ix);
                    float v = static_cast<float>(m_heightmap->heights[idx]);

                    switch (op)
                    {
                    case BrushOp::Raise:
                        v += params.strength * k * 65535.0f * 0.01f; // 1 % of range per unit strength
                        break;
                    case BrushOp::Lower:
                        v -= params.strength * k * 65535.0f * 0.01f;
                        break;
                    case BrushOp::Flatten:
                    {
                        // Target in uint16 range
                        const float target = params.flattenTarget * 65535.0f;
                        v = v + (target - v) * k * params.strength;
                        break;
                    }
                    default:
                        break;
                    }

                    m_heightmap->heights[idx] =
                        static_cast<uint16_t>(std::clamp(v, 0.0f, 65535.0f));
                }
            }
        }

        m_dirtyHeightmap = true;
        LOG_TRACE(Render, "[TerrainEditingTools] ApplyBrush op={} at ({},{}) r={}",
                  static_cast<uint32_t>(op), worldX, worldZ, params.radius);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainEditingTools::PaintSplat
    // ─────────────────────────────────────────────────────────────────────────────

    void TerrainEditingTools::PaintSplat(float worldX, float worldZ,
                                          uint32_t layer, const BrushParams& params)
    {
        if (!m_initialized || !m_splatting)
        {
            LOG_WARN(Render, "[TerrainEditingTools] PaintSplat called on uninitialised tools");
            return;
        }
        if (layer >= kSplatLayerCount)
        {
            LOG_WARN(Render, "[TerrainEditingTools] PaintSplat: layer {} >= kSplatLayerCount ({})",
                     layer, kSplatLayerCount);
            return;
        }

        std::vector<uint8_t>& splat = m_splatting->GetMutableSplatMapCpu();
        const uint32_t sw = m_splatting->GetSplatMapCpuWidth();
        const uint32_t sh = m_splatting->GetSplatMapCpuHeight();

        if (splat.empty() || sw == 0 || sh == 0)
        {
            LOG_WARN(Render, "[TerrainEditingTools] PaintSplat: empty CPU splat data");
            return;
        }

        const float worldStep = m_terrainWorldSize / static_cast<float>(sw);

        int32_t cx = 0, cz = 0;
        WorldToSplatPixel(worldX, worldZ, cx, cz);

        const int32_t iRadius = static_cast<int32_t>(params.radius / worldStep) + 1;

        for (int32_t iz = cz - iRadius; iz <= cz + iRadius; ++iz)
        {
            for (int32_t ix = cx - iRadius; ix <= cx + iRadius; ++ix)
            {
                if (ix < 0 || ix >= static_cast<int32_t>(sw) ||
                    iz < 0 || iz >= static_cast<int32_t>(sh))
                    continue;

                const float dx   = static_cast<float>(ix - cx) * worldStep;
                const float dz   = static_cast<float>(iz - cz) * worldStep;
                const float dist = std::sqrt(dx * dx + dz * dz);
                const float k    = ComputeKernel(dist, params.radius, params.falloff);
                if (k <= 0.0f) continue;

                const uint32_t base = (static_cast<uint32_t>(iz) * sw +
                                       static_cast<uint32_t>(ix)) * 4u;

                // Read current weights as floats [0, 1]
                float weights[kSplatLayerCount];
                float total = 0.0f;
                for (uint32_t li = 0; li < kSplatLayerCount; ++li)
                {
                    weights[li] = static_cast<float>(splat[base + li]) / 255.0f;
                    total       += weights[li];
                }
                // Normalise if sum is very far from 1 (guard against corrupt data)
                if (total > 0.001f && (total < 0.99f || total > 1.01f))
                {
                    for (uint32_t li = 0; li < kSplatLayerCount; ++li)
                        weights[li] /= total;
                }

                // Increase the target layer weight
                const float delta = params.strength * k;
                weights[layer] = std::min(1.0f, weights[layer] + delta);

                // Reduce other layers proportionally to keep sum = 1
                const float excess = weights[layer] - (1.0f - (1.0f - weights[layer]));
                // Simpler: renormalise all layers after boosting target
                float newTotal = 0.0f;
                for (uint32_t li = 0; li < kSplatLayerCount; ++li)
                    newTotal += weights[li];
                if (newTotal > 0.001f)
                {
                    for (uint32_t li = 0; li < kSplatLayerCount; ++li)
                        weights[li] /= newTotal;
                }
                (void)excess; // handled by renormalise above

                // Write back as uint8
                for (uint32_t li = 0; li < kSplatLayerCount; ++li)
                    splat[base + li] = static_cast<uint8_t>(
                        std::clamp(weights[li] * 255.0f, 0.0f, 255.0f));
            }
        }

        m_dirtySplatMap = true;
        LOG_TRACE(Render, "[TerrainEditingTools] PaintSplat layer={} at ({},{}) r={}",
                  layer, worldX, worldZ, params.radius);
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainEditingTools::FlushHeightmap
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainEditingTools::FlushHeightmap(VkDevice device, VkPhysicalDevice physDev,
                                              VkQueue queue, uint32_t queueFamilyIndex,
                                              VkImage heightmapImage)
    {
        if (!m_initialized || !m_heightmap)
        {
            LOG_WARN(Render, "[TerrainEditingTools] FlushHeightmap: tools not initialised");
            return false;
        }
        if (heightmapImage == VK_NULL_HANDLE)
        {
            LOG_WARN(Render, "[TerrainEditingTools] FlushHeightmap: null GPU image");
            return false;
        }

        const uint32_t w = m_heightmap->width;
        const uint32_t h = m_heightmap->height;

        if (w == 0 || h == 0 || m_heightmap->heights.empty())
        {
            LOG_WARN(Render, "[TerrainEditingTools] FlushHeightmap: empty heightmap data");
            return false;
        }

        const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * sizeof(uint16_t);

        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!CreateStagingBuffer(device, physDev, bytes, stagingBuf, stagingMem))
        {
            LOG_ERROR(Render, "[TerrainEditingTools] FlushHeightmap: staging buffer failed");
            return false;
        }

        // Map and copy CPU heightmap
        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMem, 0, bytes, 0, &mapped) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainEditingTools] FlushHeightmap: vkMapMemory failed");
            vkFreeMemory(device, stagingMem, nullptr);
            vkDestroyBuffer(device, stagingBuf, nullptr);
            return false;
        }
        std::memcpy(mapped, m_heightmap->heights.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device, stagingMem);

        // Upload via one-time command buffer (SHADER_READ_ONLY → TRANSFER_DST → back)
        const bool ok = UploadToImage(device, queue, queueFamilyIndex,
                                       stagingBuf, heightmapImage, w, h);

        vkFreeMemory(device, stagingMem, nullptr);
        vkDestroyBuffer(device, stagingBuf, nullptr);

        if (ok)
        {
            m_dirtyHeightmap = false;
            LOG_DEBUG(Render, "[TerrainEditingTools] FlushHeightmap OK ({}x{})", w, h);
        }
        else
        {
            LOG_ERROR(Render, "[TerrainEditingTools] FlushHeightmap: upload failed");
        }
        return ok;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainEditingTools::FlushSplatMap
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainEditingTools::FlushSplatMap(VkDevice device, VkPhysicalDevice physDev,
                                             VkQueue queue, uint32_t queueFamilyIndex)
    {
        if (!m_initialized || !m_splatting)
        {
            LOG_WARN(Render, "[TerrainEditingTools] FlushSplatMap: tools not initialised");
            return false;
        }

        const bool ok = m_splatting->ReuploadSplatMap(device, physDev, queue, queueFamilyIndex);
        if (ok)
        {
            m_dirtySplatMap = false;
            LOG_INFO(Render, "[TerrainEditingTools] FlushSplatMap OK");
        }
        else
        {
            LOG_ERROR(Render, "[TerrainEditingTools] FlushSplatMap: ReuploadSplatMap failed");
        }
        return ok;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainEditingTools::SaveHeightmap
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainEditingTools::SaveHeightmap(const engine::core::Config& config,
                                             const std::string& relPath)
    {
        if (!m_initialized || !m_heightmap || m_heightmap->heights.empty())
        {
            LOG_WARN(Render, "[TerrainEditingTools] SaveHeightmap: no data to save");
            return false;
        }

        const auto fullPath =
            engine::platform::FileSystem::ResolveContentPath(config, relPath);

        // Create parent directories if they do not exist
        std::error_code ec;
        std::filesystem::create_directories(fullPath.parent_path(), ec);
        if (ec)
        {
            LOG_WARN(Render, "[TerrainEditingTools] SaveHeightmap: create_directories failed: {}",
                     ec.message());
            // Not fatal — try to open anyway
        }

        std::ofstream ofs(fullPath, std::ios::binary);
        if (!ofs)
        {
            LOG_ERROR(Render, "[TerrainEditingTools] SaveHeightmap: cannot open '{}' for writing",
                      fullPath.string());
            return false;
        }

        // Binary format: magic, width, height, uint16 data (little-endian)
        const uint32_t magic  = kHeightmapMagic;
        const uint32_t width  = m_heightmap->width;
        const uint32_t height = m_heightmap->height;
        ofs.write(reinterpret_cast<const char*>(&magic),  sizeof(magic));
        ofs.write(reinterpret_cast<const char*>(&width),  sizeof(width));
        ofs.write(reinterpret_cast<const char*>(&height), sizeof(height));
        ofs.write(reinterpret_cast<const char*>(m_heightmap->heights.data()),
                  static_cast<std::streamsize>(width * height * sizeof(uint16_t)));

        if (!ofs)
        {
            LOG_ERROR(Render, "[TerrainEditingTools] SaveHeightmap: write error on '{}'",
                      fullPath.string());
            return false;
        }

        LOG_INFO(Render, "[TerrainEditingTools] SaveHeightmap OK ('{}', {}x{})",
                 relPath, width, height);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainEditingTools::SaveSplatMap
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainEditingTools::SaveSplatMap(const engine::core::Config& config,
                                            const std::string& relPath)
    {
        if (!m_initialized || !m_splatting)
        {
            LOG_WARN(Render, "[TerrainEditingTools] SaveSplatMap: tools not initialised");
            return false;
        }

        const std::vector<uint8_t>& cpuData = m_splatting->GetSplatMapCpu();
        const uint32_t sw = m_splatting->GetSplatMapCpuWidth();
        const uint32_t sh = m_splatting->GetSplatMapCpuHeight();

        if (cpuData.empty() || sw == 0 || sh == 0)
        {
            LOG_WARN(Render, "[TerrainEditingTools] SaveSplatMap: no CPU splat data");
            return false;
        }

        const auto fullPath =
            engine::platform::FileSystem::ResolveContentPath(config, relPath);

        std::error_code ec;
        std::filesystem::create_directories(fullPath.parent_path(), ec);
        if (ec)
        {
            LOG_WARN(Render, "[TerrainEditingTools] SaveSplatMap: create_directories failed: {}",
                     ec.message());
        }

        std::ofstream ofs(fullPath, std::ios::binary);
        if (!ofs)
        {
            LOG_ERROR(Render, "[TerrainEditingTools] SaveSplatMap: cannot open '{}' for writing",
                      fullPath.string());
            return false;
        }

        // Binary format: magic, width, height, RGBA8 data (little-endian)
        const uint32_t magic = kSplatFileMagic;
        ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        ofs.write(reinterpret_cast<const char*>(&sw),    sizeof(sw));
        ofs.write(reinterpret_cast<const char*>(&sh),    sizeof(sh));
        ofs.write(reinterpret_cast<const char*>(cpuData.data()),
                  static_cast<std::streamsize>(sw * sh * 4u));

        if (!ofs)
        {
            LOG_ERROR(Render, "[TerrainEditingTools] SaveSplatMap: write error on '{}'",
                      fullPath.string());
            return false;
        }

        LOG_INFO(Render, "[TerrainEditingTools] SaveSplatMap OK ('{}', {}x{})",
                 relPath, sw, sh);
        return true;
    }

} // namespace engine::render::terrain
