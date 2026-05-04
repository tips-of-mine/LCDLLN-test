#include "engine/editor/TexturePreviewCache.h"

#include "engine/core/Log.h"

// stb_image deja defini dans WorldMapIo.cpp / AssetRegistry.cpp — NE PAS redefinir
// STB_IMAGE_IMPLEMENTATION ici (link error sinon).
#include "stb_image.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <vulkan/vulkan.h>

#if defined(_WIN32)
#   include "imgui_impl_vulkan.h"
#endif
#include "engine/render/terrain/TerrainSplatting.h"

namespace engine::editor
{
    namespace
    {
        constexpr uint32_t kTexrMagic = 0x52584554u;  // "TEXR" little-endian

        /// Crop centre rectangulaire vers carre. Renvoie le pointeur vers le
        /// pixel (0,0) du sous-rectangle carre + son cote.
        void CropToSquare(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          const uint8_t*& outSrc, uint32_t& outSize, uint32_t& outRowStrideBytes)
        {
            outRowStrideBytes = srcW * 4u;
            if (srcW == srcH)
            {
                outSrc = src;
                outSize = srcW;
                return;
            }
            const uint32_t side = std::min(srcW, srcH);
            const uint32_t offX = (srcW - side) / 2u;
            const uint32_t offY = (srcH - side) / 2u;
            outSrc = src + (offY * srcW + offX) * 4u;
            outSize = side;
        }
    } // namespace

    bool ResampleRgba8Box(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                          uint32_t dstSize, std::vector<uint8_t>& outRgba)
    {
        if (src == nullptr || srcW == 0 || srcH == 0 || dstSize < 4u || dstSize > 4096u)
        {
            outRgba.clear();
            return false;
        }

        const uint8_t* sqSrc = nullptr;
        uint32_t sqSize = 0;
        uint32_t srcStride = 0;
        CropToSquare(src, srcW, srcH, sqSrc, sqSize, srcStride);

        outRgba.assign(static_cast<size_t>(dstSize) * dstSize * 4u, 0u);

        // Box filter direct (pas de mipmap chain) : pour chaque pixel dst,
        // moyenner tous les pixels src couverts. Cout O(dstSize^2 * (sqSize/dstSize)^2).
        // Pour 1024->256 : 256^2 * 16 = ~1M reads, ~3ms en debug, <1ms en release.
        for (uint32_t dy = 0; dy < dstSize; ++dy)
        {
            const uint32_t y0 = (dy * sqSize) / dstSize;
            const uint32_t y1 = ((dy + 1u) * sqSize) / dstSize;
            const uint32_t ySpan = std::max(1u, y1 - y0);
            for (uint32_t dx = 0; dx < dstSize; ++dx)
            {
                const uint32_t x0 = (dx * sqSize) / dstSize;
                const uint32_t x1 = ((dx + 1u) * sqSize) / dstSize;
                const uint32_t xSpan = std::max(1u, x1 - x0);
                uint32_t accR = 0, accG = 0, accB = 0, accA = 0;
                const uint32_t count = xSpan * ySpan;
                for (uint32_t sy = y0; sy < y0 + ySpan; ++sy)
                {
                    const uint8_t* row = sqSrc + sy * srcStride;
                    for (uint32_t sx = x0; sx < x0 + xSpan; ++sx)
                    {
                        const uint8_t* p = row + sx * 4u;
                        accR += p[0]; accG += p[1]; accB += p[2]; accA += p[3];
                    }
                }
                uint8_t* dst = outRgba.data() + (dy * dstSize + dx) * 4u;
                dst[0] = static_cast<uint8_t>(accR / count);
                dst[1] = static_cast<uint8_t>(accG / count);
                dst[2] = static_cast<uint8_t>(accB / count);
                dst[3] = static_cast<uint8_t>(accA / count);
            }
        }
        return true;
    }

    bool LoadTexrFile(const std::string& absolutePath,
                      std::vector<uint8_t>& outRgba,
                      uint32_t& outWidth, uint32_t& outHeight)
    {
        outRgba.clear();
        outWidth = 0;
        outHeight = 0;

        std::error_code ec;
        if (!std::filesystem::exists(absolutePath, ec))
        {
            LOG_ERROR(Render, "[TexturePreviewCache] Texture file not found: {}", absolutePath);
            return false;
        }

        std::ifstream f(absolutePath, std::ios::binary);
        if (!f)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] Cannot open texture file: {}", absolutePath);
            return false;
        }
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());

        // Si magic 'TEXR' present : decoder directement.
        if (data.size() >= 16u)
        {
            uint32_t magic = 0, w = 0, h = 0, srgb = 0;
            std::memcpy(&magic, data.data(), 4);
            if (magic == kTexrMagic)
            {
                std::memcpy(&w,    data.data() + 4,  4);
                std::memcpy(&h,    data.data() + 8,  4);
                std::memcpy(&srgb, data.data() + 12, 4);
                (void)srgb;
                if (w == 0 || h == 0 || w > 4096u || h > 4096u)
                {
                    LOG_ERROR(Render, "[TexturePreviewCache] TEXR invalid dims {}x{}: {}", w, h, absolutePath);
                    return false;
                }
                const size_t pixelBytes = static_cast<size_t>(w) * h * 4u;
                if (data.size() < 16u + pixelBytes)
                {
                    LOG_ERROR(Render, "[TexturePreviewCache] TEXR truncated: {}", absolutePath);
                    return false;
                }
                outRgba.assign(data.begin() + 16,
                               data.begin() + 16 + static_cast<long long>(pixelBytes));
                outWidth = w;
                outHeight = h;
                return true;
            }
        }

        // Fallback : PNG/JPG/TGA/BMP via stb_image.
        int w = 0, h = 0, comp = 0;
        stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()),
                                                &w, &h, &comp, 4);
        if (pixels == nullptr || w <= 0 || h <= 0 || w > 4096 || h > 4096)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] stb_image decode failed or out of range: {}", absolutePath);
            if (pixels) stbi_image_free(pixels);
            return false;
        }
        const size_t pixelBytes = static_cast<size_t>(w) * h * 4u;
        outRgba.assign(pixels, pixels + pixelBytes);
        outWidth = static_cast<uint32_t>(w);
        outHeight = static_cast<uint32_t>(h);
        stbi_image_free(pixels);
        return true;
    }

#if defined(_WIN32)
    TexturePreviewCache::~TexturePreviewCache()
    {
        Shutdown();
    }

    bool TexturePreviewCache::Init(VkDevice device, VkPhysicalDevice physDev,
                                    VkQueue queue, uint32_t queueFamilyIndex,
                                    const std::string& contentDir)
    {
        if (m_ready) return true;
        if (device == nullptr || physDev == nullptr || queue == nullptr)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] Init: invalid Vulkan handles");
            return false;
        }
        m_device = device;
        m_physDev = physDev;
        m_queue = queue;
        m_queueFamily = queueFamilyIndex;
        m_contentDir = contentDir;

        // Sampler partage : linear filter, clamp to edge. Identique pour toutes
        // les vignettes du cache.
        VkSamplerCreateInfo si{};
        si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter     = VK_FILTER_LINEAR;
        si.minFilter     = VK_FILTER_LINEAR;
        si.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxAnisotropy = 1.0f;
        si.minLod        = 0.0f;
        si.maxLod        = 0.0f;
        if (vkCreateSampler(m_device, &si, nullptr, &m_sampler) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] vkCreateSampler failed");
            return false;
        }

        // Descriptor pool dedie : 64 sets max, 1 sampled image chacun (pour ImGui).
        // Au-dela : warning cote alloc, vignette grise.
        VkDescriptorPoolSize ps{};
        ps.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 64u;

        VkDescriptorPoolCreateInfo pi{};
        pi.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pi.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets       = 64u;
        pi.poolSizeCount = 1;
        pi.pPoolSizes    = &ps;
        if (vkCreateDescriptorPool(m_device, &pi, nullptr, &m_pool) != VK_SUCCESS)
        {
            vkDestroySampler(m_device, m_sampler, nullptr);
            m_sampler = nullptr;
            LOG_ERROR(Render, "[TexturePreviewCache] vkCreateDescriptorPool failed");
            return false;
        }

        m_ready = true;
        LOG_INFO(Render, "[TexturePreviewCache] Init OK (contentDir={})", m_contentDir);
        return true;
    }

    void TexturePreviewCache::Shutdown()
    {
        if (m_device != nullptr)
        {
            for (auto& kv : m_entries) DestroyEntry(kv.second);
            m_entries.clear();
            if (m_pool != nullptr)
            {
                vkDestroyDescriptorPool(m_device, m_pool, nullptr);
                m_pool = nullptr;
            }
            if (m_sampler != nullptr)
            {
                vkDestroySampler(m_device, m_sampler, nullptr);
                m_sampler = nullptr;
            }
        }
        m_device = nullptr;
        m_physDev = nullptr;
        m_queue = nullptr;
        m_queueFamily = 0;
        m_contentDir.clear();
        m_ready = false;
    }

    std::string TexturePreviewCache::ProceduralKey(uint32_t layer)
    {
        return std::string("procedural:") + std::to_string(layer);
    }

    ImTextureID TexturePreviewCache::CreateEntry(const std::string& key,
                                                  const std::vector<uint8_t>& rgba256)
    {
        constexpr uint32_t kRes = engine::render::terrain::kSplatLayerResolution;
        const size_t expectedBytes = static_cast<size_t>(kRes) * kRes * 4u;
        if (!m_ready || rgba256.size() != expectedBytes)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] CreateEntry({}): not ready or bad size {} (expected {})",
                      key, rgba256.size(), expectedBytes);
            return nullptr;
        }

        GpuPreview preview;
        preview.cpuRgba256 = rgba256;

        // 1) VkImage 256x256 RGBA8_SRGB, OPTIMAL, SAMPLED + TRANSFER_DST.
        VkImageCreateInfo ici{};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = VK_FORMAT_R8G8B8A8_SRGB;
        ici.extent        = { kRes, kRes, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(m_device, &ici, nullptr, &preview.image) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] vkCreateImage failed for {}", key);
            return nullptr;
        }

        VkMemoryRequirements mr{};
        vkGetImageMemoryRequirements(m_device, preview.image, &mr);
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(m_physDev, &mp);
        uint32_t memType = UINT32_MAX;
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        {
            if ((mr.memoryTypeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                memType = i;
                break;
            }
        }
        if (memType == UINT32_MAX)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] no DEVICE_LOCAL memory for {}", key);
            vkDestroyImage(m_device, preview.image, nullptr);
            return nullptr;
        }
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = memType;
        if (vkAllocateMemory(m_device, &ai, nullptr, &preview.memory) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] vkAllocateMemory failed for {}", key);
            vkDestroyImage(m_device, preview.image, nullptr);
            return nullptr;
        }
        vkBindImageMemory(m_device, preview.image, preview.memory, 0);

        // 2) Staging buffer (HOST_VISIBLE | HOST_COHERENT).
        VkBuffer staging = nullptr;
        VkDeviceMemory stagingMem = nullptr;
        VkBufferCreateInfo bci{};
        bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size        = rgba256.size();
        bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bci, nullptr, &staging) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] staging vkCreateBuffer failed for {}", key);
            vkFreeMemory(m_device, preview.memory, nullptr);
            vkDestroyImage(m_device, preview.image, nullptr);
            return nullptr;
        }
        VkMemoryRequirements bmr{};
        vkGetBufferMemoryRequirements(m_device, staging, &bmr);
        uint32_t hvis = UINT32_MAX;
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        {
            const VkMemoryPropertyFlags need =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((bmr.memoryTypeBits & (1u << i)) &&
                (mp.memoryTypes[i].propertyFlags & need) == need)
            {
                hvis = i;
                break;
            }
        }
        if (hvis == UINT32_MAX)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] no HOST_VISIBLE memory for {}", key);
            vkDestroyBuffer(m_device, staging, nullptr);
            vkFreeMemory(m_device, preview.memory, nullptr);
            vkDestroyImage(m_device, preview.image, nullptr);
            return nullptr;
        }
        VkMemoryAllocateInfo ai2{};
        ai2.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai2.allocationSize  = bmr.size;
        ai2.memoryTypeIndex = hvis;
        if (vkAllocateMemory(m_device, &ai2, nullptr, &stagingMem) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] staging vkAllocateMemory failed for {}", key);
            vkDestroyBuffer(m_device, staging, nullptr);
            vkFreeMemory(m_device, preview.memory, nullptr);
            vkDestroyImage(m_device, preview.image, nullptr);
            return nullptr;
        }
        vkBindBufferMemory(m_device, staging, stagingMem, 0);
        void* mapped = nullptr;
        vkMapMemory(m_device, stagingMem, 0, rgba256.size(), 0, &mapped);
        std::memcpy(mapped, rgba256.data(), rgba256.size());
        vkUnmapMemory(m_device, stagingMem);

        // 3) One-shot command pool + buffer : barriers + copy.
        VkCommandPool pool = nullptr;
        VkCommandPoolCreateInfo pci{};
        pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.queueFamilyIndex = m_queueFamily;
        pci.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        vkCreateCommandPool(m_device, &pci, nullptr, &pool);
        VkCommandBuffer cmd = nullptr;
        VkCommandBufferAllocateInfo cai{};
        cai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool        = pool;
        cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_device, &cai, &cmd);
        VkCommandBufferBeginInfo cbb{};
        cbb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cbb.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &cbb);

        VkImageMemoryBarrier b1{};
        b1.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b1.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
        b1.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b1.image            = preview.image;
        b1.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b1.srcAccessMask    = 0;
        b1.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b1);

        VkBufferImageCopy r{};
        r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        r.imageExtent      = { kRes, kRes, 1 };
        vkCmdCopyBufferToImage(cmd, staging, preview.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);

        VkImageMemoryBarrier b2 = b1;
        b2.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b2.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &b2);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo sub{};
        sub.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        sub.commandBufferCount = 1;
        sub.pCommandBuffers    = &cmd;
        vkQueueSubmit(m_queue, 1, &sub, nullptr);
        vkQueueWaitIdle(m_queue);

        vkDestroyCommandPool(m_device, pool, nullptr);
        vkDestroyBuffer(m_device, staging, nullptr);
        vkFreeMemory(m_device, stagingMem, nullptr);

        // 4) ImageView.
        VkImageViewCreateInfo vci{};
        vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image            = preview.image;
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.format           = VK_FORMAT_R8G8B8A8_SRGB;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(m_device, &vci, nullptr, &preview.view) != VK_SUCCESS)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] vkCreateImageView failed for {}", key);
            vkFreeMemory(m_device, preview.memory, nullptr);
            vkDestroyImage(m_device, preview.image, nullptr);
            return nullptr;
        }

        // 5) Descriptor ImGui (alloue par le backend ImGui depuis son propre pool).
        preview.imguiDS = ImGui_ImplVulkan_AddTexture(m_sampler, preview.view,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (preview.imguiDS == nullptr)
        {
            LOG_ERROR(Render, "[TexturePreviewCache] ImGui_ImplVulkan_AddTexture failed for {}", key);
            vkDestroyImageView(m_device, preview.view, nullptr);
            vkFreeMemory(m_device, preview.memory, nullptr);
            vkDestroyImage(m_device, preview.image, nullptr);
            return nullptr;
        }

        const ImTextureID id = static_cast<ImTextureID>(preview.imguiDS);
        m_entries.emplace(key, std::move(preview));
        return id;
    }

    void TexturePreviewCache::DestroyEntry(GpuPreview& p)
    {
        if (p.imguiDS != nullptr)
        {
            ImGui_ImplVulkan_RemoveTexture(p.imguiDS);
            p.imguiDS = nullptr;
        }
        if (p.view != nullptr)   { vkDestroyImageView(m_device, p.view, nullptr);   p.view = nullptr; }
        if (p.image != nullptr)  { vkDestroyImage(m_device, p.image, nullptr);      p.image = nullptr; }
        if (p.memory != nullptr) { vkFreeMemory(m_device, p.memory, nullptr);       p.memory = nullptr; }
        p.cpuRgba256.clear();
    }

    ImTextureID TexturePreviewCache::GetProceduralThumb(uint32_t layer)
    {
        if (!m_ready || layer >= engine::render::terrain::kSplatLayerCount) return nullptr;
        const std::string key = ProceduralKey(layer);
        auto it = m_entries.find(key);
        if (it != m_entries.end())
        {
            return static_cast<ImTextureID>(it->second.imguiDS);
        }
        std::vector<uint8_t> rgba;
        if (!engine::render::terrain::GenerateProceduralAlbedoLayer(
                engine::render::terrain::kSplatLayerResolution, layer, rgba))
        {
            return nullptr;
        }
        return CreateEntry(key, rgba);
    }

    ImTextureID TexturePreviewCache::GetTexrThumb(const std::string& contentRelPath)
    {
        if (!m_ready || contentRelPath.empty()) return nullptr;
        auto it = m_entries.find(contentRelPath);
        if (it != m_entries.end())
        {
            return static_cast<ImTextureID>(it->second.imguiDS);
        }
        if (m_negativeCache.count(contentRelPath) != 0)
        {
            return nullptr; // deja echoue, ne pas spam les logs
        }

        // Resoudre en chemin absolu via m_contentDir.
        const std::filesystem::path abs =
            std::filesystem::path(m_contentDir) / contentRelPath;

        std::vector<uint8_t> raw;
        uint32_t srcW = 0, srcH = 0;
        if (!LoadTexrFile(abs.string(), raw, srcW, srcH))
        {
            m_negativeCache.insert(contentRelPath);
            return nullptr;
        }

        std::vector<uint8_t> resampled;
        if (!ResampleRgba8Box(raw.data(), srcW, srcH,
                              engine::render::terrain::kSplatLayerResolution, resampled))
        {
            LOG_ERROR(Render, "[TexturePreviewCache] ResampleRgba8Box failed for {}", contentRelPath);
            m_negativeCache.insert(contentRelPath);
            return nullptr;
        }

        return CreateEntry(contentRelPath, resampled);
    }

    const std::vector<uint8_t>* TexturePreviewCache::GetCpuRgba256(const std::string& key) const
    {
        auto it = m_entries.find(key);
        if (it == m_entries.end()) return nullptr;
        return &it->second.cpuRgba256;
    }

#else
    // No-op stubs pour Linux/macOS — l'editeur monde ne tourne que sur Win32.
    TexturePreviewCache::~TexturePreviewCache() = default;
    bool TexturePreviewCache::Init(VkDevice, VkPhysicalDevice, VkQueue, uint32_t, const std::string&) { return false; }
    void TexturePreviewCache::Shutdown() {}
    std::string TexturePreviewCache::ProceduralKey(uint32_t layer) { return std::string("procedural:") + std::to_string(layer); }
    ImTextureID TexturePreviewCache::CreateEntry(const std::string&, const std::vector<uint8_t>&) { return nullptr; }
    void TexturePreviewCache::DestroyEntry(GpuPreview&) {}
    ImTextureID TexturePreviewCache::GetProceduralThumb(uint32_t) { return nullptr; }
    ImTextureID TexturePreviewCache::GetTexrThumb(const std::string&) { return nullptr; }
    const std::vector<uint8_t>* TexturePreviewCache::GetCpuRgba256(const std::string&) const { return nullptr; }
#endif

} // namespace engine::editor
