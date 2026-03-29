#include "engine/render/terrain/TerrainSplatting.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cstring>
#include <vector>

namespace engine::render::terrain
{
    // ─────────────────────────────────────────────────────────────────────────────
    // Internal Vulkan helpers
    // ─────────────────────────────────────────────────────────────────────────────
    namespace
    {
        /// Finds a memory type index that satisfies the given property flags.
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

        /// Creates a HOST_VISIBLE | HOST_COHERENT staging buffer.
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
                LOG_ERROR(Render, "[TerrainSplatting] vkCreateBuffer (staging) failed");
                return false;
            }

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, outBuf, &req);

            const uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (memType == UINT32_MAX)
            {
                LOG_ERROR(Render, "[TerrainSplatting] No HOST_VISIBLE memory for staging");
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
                LOG_ERROR(Render, "[TerrainSplatting] vkAllocateMemory (staging) failed");
                vkDestroyBuffer(device, outBuf, nullptr);
                outBuf = VK_NULL_HANDLE;
                return false;
            }

            if (vkBindBufferMemory(device, outBuf, outMem, 0) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainSplatting] vkBindBufferMemory (staging) failed");
                vkFreeMemory(device, outMem, nullptr);
                vkDestroyBuffer(device, outBuf, nullptr);
                outBuf = VK_NULL_HANDLE;
                outMem = VK_NULL_HANDLE;
                return false;
            }
            return true;
        }

        /// Creates a DEVICE_LOCAL OPTIMAL image (array-capable).
        bool CreateOptimalImage(VkDevice device, VkPhysicalDevice physDev,
                                uint32_t width, uint32_t height, uint32_t layerCount,
                                VkFormat format, VkImageUsageFlags usage,
                                VkImage& outImage, VkDeviceMemory& outMemory)
        {
            VkImageCreateInfo ici{};
            ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ici.imageType     = VK_IMAGE_TYPE_2D;
            ici.format        = format;
            ici.extent        = { width, height, 1 };
            ici.mipLevels     = 1;
            ici.arrayLayers   = layerCount;
            ici.samples       = VK_SAMPLE_COUNT_1_BIT;
            ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ici.usage         = usage;
            ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateImage(device, &ici, nullptr, &outImage) != VK_SUCCESS ||
                outImage == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainSplatting] vkCreateImage failed ({}x{} layers={})",
                          width, height, layerCount);
                return false;
            }

            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(device, outImage, &req);

            const uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (memType == UINT32_MAX)
            {
                LOG_ERROR(Render, "[TerrainSplatting] No DEVICE_LOCAL memory for image");
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
                LOG_ERROR(Render, "[TerrainSplatting] vkAllocateMemory (image) failed");
                vkDestroyImage(device, outImage, nullptr);
                outImage = VK_NULL_HANDLE;
                return false;
            }

            if (vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainSplatting] vkBindImageMemory failed");
                vkFreeMemory(device, outMemory, nullptr);
                vkDestroyImage(device, outImage, nullptr);
                outImage  = VK_NULL_HANDLE;
                outMemory = VK_NULL_HANDLE;
                return false;
            }
            return true;
        }

        /// Submits a one-time command buffer: barrier → copies → barrier, then waits.
        /// Supports multi-layer images via the provided array of copy regions.
        bool UploadViaStaging(VkDevice device, VkQueue queue, uint32_t queueFamilyIndex,
                              VkBuffer stagingBuffer,
                              VkImage dstImage, uint32_t layerCount,
                              const VkBufferImageCopy* regions)
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
                    LOG_ERROR(Render, "[TerrainSplatting] vkCreateCommandPool failed");
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
                    LOG_ERROR(Render, "[TerrainSplatting] vkAllocateCommandBuffers failed");
                    vkDestroyCommandPool(device, pool, nullptr);
                    return false;
                }
            }

            VkCommandBufferBeginInfo beginInfo{};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainSplatting] vkBeginCommandBuffer failed");
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            // Barrier: UNDEFINED → TRANSFER_DST_OPTIMAL (all layers at once)
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image               = dstImage;
                barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layerCount };
                barrier.srcAccessMask       = 0;
                barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            // Copy all layers
            vkCmdCopyBufferToImage(cmd, stagingBuffer, dstImage,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   layerCount, regions);

            // Barrier: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL (all layers)
            {
                VkImageMemoryBarrier barrier{};
                barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image               = dstImage;
                barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layerCount };
                barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainSplatting] vkEndCommandBuffer failed");
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            VkFence fence = VK_NULL_HANDLE;
            {
                VkFenceCreateInfo fenceCI{};
                fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                if (vkCreateFence(device, &fenceCI, nullptr, &fence) != VK_SUCCESS)
                {
                    LOG_ERROR(Render, "[TerrainSplatting] vkCreateFence failed");
                    vkDestroyCommandPool(device, pool, nullptr);
                    return false;
                }
            }

            VkSubmitInfo si{};
            si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers    = &cmd;

            if (vkQueueSubmit(queue, 1, &si, fence) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainSplatting] vkQueueSubmit failed");
                vkDestroyFence(device, fence, nullptr);
                vkDestroyCommandPool(device, pool, nullptr);
                return false;
            }

            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device, fence, nullptr);
            vkDestroyCommandPool(device, pool, nullptr);
            return true;
        }
    } // namespace

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainSplatting::GetLayerTiling
    // ─────────────────────────────────────────────────────────────────────────────

    float TerrainSplatting::GetLayerTiling(uint32_t layer) const
    {
        return (layer < kSplatLayerCount) ? m_layerTiling[layer] : 8.0f;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainSplatting::Init
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainSplatting::Init(VkDevice device, VkPhysicalDevice physDev,
                                const engine::core::Config& config,
                                const std::string& splatmapRelPath,
                                VkQueue queue, uint32_t queueFamilyIndex)
    {
        LOG_INFO(Render, "[TerrainSplatting] Init begin (splatmap='{}')", splatmapRelPath);

        if (device == VK_NULL_HANDLE || physDev == VK_NULL_HANDLE)
        {
            LOG_ERROR(Render, "[TerrainSplatting] Init: invalid device parameters");
            return false;
        }

        // ── Read tiling config ────────────────────────────────────────────────────
        m_layerTiling[0] = static_cast<float>(config.GetDouble("terrain.splat.tiling_grass",  8.0));
        m_layerTiling[1] = static_cast<float>(config.GetDouble("terrain.splat.tiling_dirt",   8.0));
        m_layerTiling[2] = static_cast<float>(config.GetDouble("terrain.splat.tiling_rock",  16.0));
        m_layerTiling[3] = static_cast<float>(config.GetDouble("terrain.splat.tiling_snow",  12.0));

        for (uint32_t i = 0; i < kSplatLayerCount; ++i)
        {
            if (m_layerTiling[i] <= 0.0f)
            {
                LOG_WARN(Render, "[TerrainSplatting] Layer {} tiling <= 0 ({}); clamped to 8.0",
                         i, m_layerTiling[i]);
                m_layerTiling[i] = 8.0f;
            }
        }

        LOG_DEBUG(Render,
            "[TerrainSplatting] Tiling: grass={} dirt={} rock={} snow={}",
            m_layerTiling[0], m_layerTiling[1], m_layerTiling[2], m_layerTiling[3]);

        // ── Build default splat map (all grass: R=255, G=B=A=0) ──────────────────
        // MVP: generate a flat splat map. If splatmapRelPath is provided and the
        // binary format is available, it could be loaded here in a future ticket.
        (void)splatmapRelPath; // reserved for future file loading
        constexpr uint32_t kSplatW = 1024u;
        constexpr uint32_t kSplatH = 1024u;

        std::vector<uint8_t> splatData(kSplatW * kSplatH * 4u, 0u);
        for (uint32_t i = 0; i < kSplatW * kSplatH; ++i)
        {
            // RGBA: R=grass weight, G=dirt, B=rock, A=snow
            splatData[i * 4 + 0] = 255u; // grass = 1.0
            splatData[i * 4 + 1] = 0u;
            splatData[i * 4 + 2] = 0u;
            splatData[i * 4 + 3] = 0u;
        }
        LOG_DEBUG(Render, "[TerrainSplatting] Generated default splat map ({}x{}, all grass)",
                  kSplatW, kSplatH);

        // ── Keep CPU copy for M34.4 editing tools ─────────────────────────────────
        m_cpuData   = splatData;
        m_cpuWidth  = kSplatW;
        m_cpuHeight = kSplatH;

        // ── Upload splat map ──────────────────────────────────────────────────────
        if (!UploadSplatMap(device, physDev, splatData, kSplatW, kSplatH,
                            queue, queueFamilyIndex, m_splatMap))
        {
            LOG_ERROR(Render, "[TerrainSplatting] Failed to upload splat map");
            Destroy(device);
            return false;
        }

        // ── Build placeholder texture arrays ──────────────────────────────────────
        // Each array layer is a 4×4 solid-colour tile. Layers: grass, dirt, rock, snow.
        constexpr uint32_t kTexW = 4u;
        constexpr uint32_t kTexH = 4u;
        constexpr uint32_t kPixels = kTexW * kTexH;

        // Layer albedo colours (RGBA8, sRGB-ish but stored linear for GPU)
        // 0=grass, 1=dirt, 2=rock, 3=snow
        static const uint8_t kAlbedo[kSplatLayerCount][4] = {
            {  89u, 140u,  51u, 255u },   // grass — green
            { 128u,  82u,  38u, 255u },   // dirt  — brown
            { 128u, 107u,  82u, 255u },   // rock  — grey-brown
            { 242u, 242u, 250u, 255u },   // snow  — near-white
        };

        // Flat normal (pointing up in tangent space: RGB=128,128,255)
        static const uint8_t kNormal[4] = { 128u, 128u, 255u, 255u };

        // ORM: R=AO(255=1.0), G=Roughness(204≈0.8), B=Metallic(0)
        static const uint8_t kORM[4] = { 255u, 204u, 0u, 255u };

        // Build albedo array data (layerCount × width × height × 4 bytes)
        {
            std::vector<uint8_t> albedoData(kSplatLayerCount * kPixels * 4u);
            for (uint32_t layer = 0; layer < kSplatLayerCount; ++layer)
            {
                const uint8_t* col = kAlbedo[layer];
                for (uint32_t p = 0; p < kPixels; ++p)
                {
                    uint8_t* dst = albedoData.data() + (layer * kPixels + p) * 4u;
                    dst[0] = col[0]; dst[1] = col[1]; dst[2] = col[2]; dst[3] = col[3];
                }
            }
            if (!UploadTextureArray(device, physDev, albedoData, kTexW, kTexH, kSplatLayerCount,
                                    queue, queueFamilyIndex, m_albedoArray))
            {
                LOG_ERROR(Render, "[TerrainSplatting] Failed to upload albedo array");
                Destroy(device);
                return false;
            }
        }

        // Build normal array data
        {
            std::vector<uint8_t> normalData(kSplatLayerCount * kPixels * 4u);
            for (uint32_t layer = 0; layer < kSplatLayerCount; ++layer)
                for (uint32_t p = 0; p < kPixels; ++p)
                {
                    uint8_t* dst = normalData.data() + (layer * kPixels + p) * 4u;
                    dst[0] = kNormal[0]; dst[1] = kNormal[1];
                    dst[2] = kNormal[2]; dst[3] = kNormal[3];
                }
            if (!UploadTextureArray(device, physDev, normalData, kTexW, kTexH, kSplatLayerCount,
                                    queue, queueFamilyIndex, m_normalArray))
            {
                LOG_ERROR(Render, "[TerrainSplatting] Failed to upload normal array");
                Destroy(device);
                return false;
            }
        }

        // Build ORM array data
        {
            std::vector<uint8_t> ormData(kSplatLayerCount * kPixels * 4u);
            for (uint32_t layer = 0; layer < kSplatLayerCount; ++layer)
                for (uint32_t p = 0; p < kPixels; ++p)
                {
                    uint8_t* dst = ormData.data() + (layer * kPixels + p) * 4u;
                    dst[0] = kORM[0]; dst[1] = kORM[1]; dst[2] = kORM[2]; dst[3] = kORM[3];
                }
            if (!UploadTextureArray(device, physDev, ormData, kTexW, kTexH, kSplatLayerCount,
                                    queue, queueFamilyIndex, m_ormArray))
            {
                LOG_ERROR(Render, "[TerrainSplatting] Failed to upload ORM array");
                Destroy(device);
                return false;
            }
        }

        LOG_INFO(Render,
            "[TerrainSplatting] Init OK (splatmap={}x{}, texArrays={}x{}x{}layers)",
            kSplatW, kSplatH, kTexW, kTexH, kSplatLayerCount);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainSplatting::Destroy
    // ─────────────────────────────────────────────────────────────────────────────

    void TerrainSplatting::Destroy(VkDevice device)
    {
        DestroySplatMap(device, m_splatMap);
        DestroyTextureArray(device, m_albedoArray);
        DestroyTextureArray(device, m_normalArray);
        DestroyTextureArray(device, m_ormArray);
        m_cpuData.clear();
        m_cpuWidth  = 0;
        m_cpuHeight = 0;
        LOG_INFO(Render, "[TerrainSplatting] Destroyed");
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainSplatting::ReuploadSplatMap  (M34.4)
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainSplatting::ReuploadSplatMap(VkDevice device, VkPhysicalDevice physDev,
                                             VkQueue queue, uint32_t queueFamilyIndex)
    {
        if (m_cpuData.empty() || m_cpuWidth == 0 || m_cpuHeight == 0)
        {
            LOG_WARN(Render, "[TerrainSplatting] ReuploadSplatMap: no CPU data available");
            return false;
        }
        if (m_splatMap.image == VK_NULL_HANDLE)
        {
            LOG_WARN(Render, "[TerrainSplatting] ReuploadSplatMap: GPU image not initialised");
            return false;
        }

        const VkDeviceSize dataBytes =
            static_cast<VkDeviceSize>(m_cpuWidth) * m_cpuHeight * 4u;

        // ── Staging buffer ────────────────────────────────────────────────────────
        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!CreateStagingBuffer(device, physDev, dataBytes, stagingBuf, stagingMem))
        {
            LOG_ERROR(Render, "[TerrainSplatting] ReuploadSplatMap: staging buffer failed");
            return false;
        }

        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMem, 0, dataBytes, 0, &mapped) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainSplatting] ReuploadSplatMap: vkMapMemory failed");
            vkFreeMemory(device, stagingMem, nullptr);
            vkDestroyBuffer(device, stagingBuf, nullptr);
            return false;
        }
        std::memcpy(mapped, m_cpuData.data(), static_cast<size_t>(dataBytes));
        vkUnmapMemory(device, stagingMem);

        // ── One-time command buffer ───────────────────────────────────────────────
        VkCommandPool pool = VK_NULL_HANDLE;
        {
            VkCommandPoolCreateInfo poolCI{};
            poolCI.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            poolCI.queueFamilyIndex = queueFamilyIndex;
            poolCI.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            if (vkCreateCommandPool(device, &poolCI, nullptr, &pool) != VK_SUCCESS ||
                pool == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainSplatting] ReuploadSplatMap: vkCreateCommandPool failed");
                vkFreeMemory(device, stagingMem, nullptr);
                vkDestroyBuffer(device, stagingBuf, nullptr);
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
                LOG_ERROR(Render, "[TerrainSplatting] ReuploadSplatMap: vkAllocateCommandBuffers failed");
                vkDestroyCommandPool(device, pool, nullptr);
                vkFreeMemory(device, stagingMem, nullptr);
                vkDestroyBuffer(device, stagingBuf, nullptr);
                return false;
            }
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainSplatting] ReuploadSplatMap: vkBeginCommandBuffer failed");
            vkDestroyCommandPool(device, pool, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            vkDestroyBuffer(device, stagingBuf, nullptr);
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
            b.image               = m_splatMap.image;
            b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            b.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &b);
        }

        // Copy staging → image
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
            region.imageExtent                     = { m_cpuWidth, m_cpuHeight, 1 };
            vkCmdCopyBufferToImage(cmd, stagingBuf, m_splatMap.image,
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
            b.image               = m_splatMap.image;
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
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);

        vkDestroyCommandPool(device, pool, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);
        vkDestroyBuffer(device, stagingBuf, nullptr);

        LOG_DEBUG(Render, "[TerrainSplatting] ReuploadSplatMap OK ({}x{})",
                  m_cpuWidth, m_cpuHeight);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainSplatting::UploadSplatMap
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainSplatting::UploadSplatMap(VkDevice device, VkPhysicalDevice physDev,
                                          const std::vector<uint8_t>& rgba,
                                          uint32_t width, uint32_t height,
                                          VkQueue queue, uint32_t queueFamilyIndex,
                                          SplatMapGpu& out)
    {
        const VkDeviceSize dataBytes = static_cast<VkDeviceSize>(width) * height * 4u;

        // Create + fill staging buffer
        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!CreateStagingBuffer(device, physDev, dataBytes, stagingBuf, stagingMem))
            return false;

        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMem, 0, dataBytes, 0, &mapped) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainSplatting] vkMapMemory (splat staging) failed");
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }
        std::memcpy(mapped, rgba.data(), static_cast<size_t>(dataBytes));
        vkUnmapMemory(device, stagingMem);

        // Create DEVICE_LOCAL RGBA8 image (single layer)
        if (!CreateOptimalImage(device, physDev, width, height, 1u,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                out.image, out.memory))
        {
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }

        // Upload (single copy region, single layer)
        VkBufferImageCopy region{};
        region.bufferOffset      = 0;
        region.bufferRowLength   = width;
        region.bufferImageHeight = height;
        region.imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageOffset       = { 0, 0, 0 };
        region.imageExtent       = { width, height, 1 };

        if (!UploadViaStaging(device, queue, queueFamilyIndex, stagingBuf, out.image, 1u, &region))
        {
            LOG_ERROR(Render, "[TerrainSplatting] Splat map staging upload failed");
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            DestroySplatMap(device, out);
            return false;
        }

        vkDestroyBuffer(device, stagingBuf, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);

        // Image view (2D, single layer)
        VkImageViewCreateInfo viewCI{};
        viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image                           = out.image;
        viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format                          = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.baseMipLevel   = 0;
        viewCI.subresourceRange.levelCount     = 1;
        viewCI.subresourceRange.baseArrayLayer = 0;
        viewCI.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device, &viewCI, nullptr, &out.view) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainSplatting] vkCreateImageView (splat) failed");
            DestroySplatMap(device, out);
            return false;
        }

        // Sampler: linear, clamp-to-edge (splat map covers exactly [0,1] UV space)
        VkSamplerCreateInfo sampCI{};
        sampCI.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampCI.magFilter    = VK_FILTER_LINEAR;
        sampCI.minFilter    = VK_FILTER_LINEAR;
        sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampCI.maxLod       = 0.0f;

        if (vkCreateSampler(device, &sampCI, nullptr, &out.sampler) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainSplatting] vkCreateSampler (splat) failed");
            DestroySplatMap(device, out);
            return false;
        }

        out.width  = width;
        out.height = height;
        LOG_DEBUG(Render, "[TerrainSplatting] SplatMap uploaded OK ({}x{})", width, height);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainSplatting::UploadTextureArray
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainSplatting::UploadTextureArray(VkDevice device, VkPhysicalDevice physDev,
                                              const std::vector<uint8_t>& rgba,
                                              uint32_t width, uint32_t height,
                                              uint32_t layerCount,
                                              VkQueue queue, uint32_t queueFamilyIndex,
                                              TextureArrayGpu& out)
    {
        const VkDeviceSize layerBytes = static_cast<VkDeviceSize>(width) * height * 4u;
        const VkDeviceSize totalBytes = layerBytes * layerCount;

        // Create + fill staging buffer (all layers contiguous)
        VkBuffer       stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!CreateStagingBuffer(device, physDev, totalBytes, stagingBuf, stagingMem))
            return false;

        void* mapped = nullptr;
        if (vkMapMemory(device, stagingMem, 0, totalBytes, 0, &mapped) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainSplatting] vkMapMemory (texArray staging) failed");
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }
        std::memcpy(mapped, rgba.data(), static_cast<size_t>(totalBytes));
        vkUnmapMemory(device, stagingMem);

        // Create DEVICE_LOCAL RGBA8 image array
        if (!CreateOptimalImage(device, physDev, width, height, layerCount,
                                VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                out.image, out.memory))
        {
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            return false;
        }

        // Build one copy region per layer
        std::vector<VkBufferImageCopy> regions(layerCount);
        for (uint32_t layer = 0; layer < layerCount; ++layer)
        {
            regions[layer].bufferOffset      = layerBytes * layer;
            regions[layer].bufferRowLength   = width;
            regions[layer].bufferImageHeight = height;
            regions[layer].imageSubresource  = { VK_IMAGE_ASPECT_COLOR_BIT, 0, layer, 1 };
            regions[layer].imageOffset       = { 0, 0, 0 };
            regions[layer].imageExtent       = { width, height, 1 };
        }

        if (!UploadViaStaging(device, queue, queueFamilyIndex,
                              stagingBuf, out.image, layerCount, regions.data()))
        {
            LOG_ERROR(Render, "[TerrainSplatting] Texture array staging upload failed");
            vkDestroyBuffer(device, stagingBuf, nullptr);
            vkFreeMemory(device, stagingMem, nullptr);
            DestroyTextureArray(device, out);
            return false;
        }

        vkDestroyBuffer(device, stagingBuf, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);

        // Image view (2D_ARRAY, all layers)
        VkImageViewCreateInfo viewCI{};
        viewCI.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image                           = out.image;
        viewCI.viewType                        = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewCI.format                          = VK_FORMAT_R8G8B8A8_UNORM;
        viewCI.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.baseMipLevel   = 0;
        viewCI.subresourceRange.levelCount     = 1;
        viewCI.subresourceRange.baseArrayLayer = 0;
        viewCI.subresourceRange.layerCount     = layerCount;

        if (vkCreateImageView(device, &viewCI, nullptr, &out.view) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainSplatting] vkCreateImageView (texArray) failed");
            DestroyTextureArray(device, out);
            return false;
        }

        // Sampler: linear, repeat (texture tiles across terrain)
        VkSamplerCreateInfo sampCI{};
        sampCI.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampCI.magFilter    = VK_FILTER_LINEAR;
        sampCI.minFilter    = VK_FILTER_LINEAR;
        sampCI.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampCI.maxLod       = 0.0f;

        if (vkCreateSampler(device, &sampCI, nullptr, &out.sampler) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TerrainSplatting] vkCreateSampler (texArray) failed");
            DestroyTextureArray(device, out);
            return false;
        }

        out.width      = width;
        out.height     = height;
        out.layerCount = layerCount;
        LOG_DEBUG(Render, "[TerrainSplatting] TextureArray uploaded OK ({}x{} layers={})",
                  width, height, layerCount);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainSplatting::DestroySplatMap / DestroyTextureArray
    // ─────────────────────────────────────────────────────────────────────────────

    void TerrainSplatting::DestroySplatMap(VkDevice device, SplatMapGpu& g)
    {
        if (g.sampler != VK_NULL_HANDLE) { vkDestroySampler(device, g.sampler, nullptr);   g.sampler = VK_NULL_HANDLE; }
        if (g.view    != VK_NULL_HANDLE) { vkDestroyImageView(device, g.view, nullptr);    g.view    = VK_NULL_HANDLE; }
        if (g.image   != VK_NULL_HANDLE) { vkDestroyImage(device, g.image, nullptr);       g.image   = VK_NULL_HANDLE; }
        if (g.memory  != VK_NULL_HANDLE) { vkFreeMemory(device, g.memory, nullptr);        g.memory  = VK_NULL_HANDLE; }
        g.width = g.height = 0;
    }

    void TerrainSplatting::DestroyTextureArray(VkDevice device, TextureArrayGpu& g)
    {
        if (g.sampler != VK_NULL_HANDLE) { vkDestroySampler(device, g.sampler, nullptr);   g.sampler    = VK_NULL_HANDLE; }
        if (g.view    != VK_NULL_HANDLE) { vkDestroyImageView(device, g.view, nullptr);    g.view       = VK_NULL_HANDLE; }
        if (g.image   != VK_NULL_HANDLE) { vkDestroyImage(device, g.image, nullptr);       g.image      = VK_NULL_HANDLE; }
        if (g.memory  != VK_NULL_HANDLE) { vkFreeMemory(device, g.memory, nullptr);        g.memory     = VK_NULL_HANDLE; }
        g.width = g.height = g.layerCount = 0;
    }

} // namespace engine::render::terrain
