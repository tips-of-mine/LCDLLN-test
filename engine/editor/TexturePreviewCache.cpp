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
#else
    // No-op stubs pour Linux/macOS — l'editeur monde ne tourne que sur Win32.
    TexturePreviewCache::~TexturePreviewCache() = default;
    bool TexturePreviewCache::Init(VkDevice, VkPhysicalDevice, VkQueue, uint32_t, const std::string&) { return false; }
    void TexturePreviewCache::Shutdown() {}
#endif

} // namespace engine::editor
