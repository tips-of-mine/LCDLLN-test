#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace engine::core { class Config; }

namespace engine::render::terrain
{
    /// Number of splat layers (grass, dirt, rock, snow).
    static constexpr uint32_t kSplatLayerCount = 4u;

    /// GPU RGBA8_UNORM splat map texture (DEVICE_LOCAL, OPTIMAL tiling).
    /// Channel mapping: R=grass, G=dirt, B=rock, A=snow.
    struct SplatMapGpu
    {
        VkImage        image   = VK_NULL_HANDLE;
        VkImageView    view    = VK_NULL_HANDLE;
        VkDeviceMemory memory  = VK_NULL_HANDLE;
        VkSampler      sampler = VK_NULL_HANDLE;
        uint32_t       width   = 0;
        uint32_t       height  = 0;
    };

    /// GPU RGBA8_UNORM texture array (kSplatLayerCount layers, DEVICE_LOCAL, OPTIMAL tiling).
    /// Layer order: 0=grass, 1=dirt, 2=rock, 3=snow.
    struct TextureArrayGpu
    {
        VkImage        image      = VK_NULL_HANDLE;
        VkImageView    view       = VK_NULL_HANDLE;
        VkDeviceMemory memory     = VK_NULL_HANDLE;
        VkSampler      sampler    = VK_NULL_HANDLE;
        uint32_t       width      = 0;
        uint32_t       height     = 0;
        uint32_t       layerCount = 0;
    };

    /// Manages terrain texture splatting resources:
    ///   - Splat map (RGBA8, 1024×1024): R=grass, G=dirt, B=rock, A=snow
    ///   - Three texture arrays (albedo, normal, ORM), each with kSplatLayerCount layers
    ///   - Per-layer tiling scale in world metres per tile
    ///
    /// If the splat map file is absent or unreadable, a default is generated (all grass).
    /// Texture array layers are generated as solid-colour placeholders (MVP).
    class TerrainSplatting
    {
    public:
        TerrainSplatting() = default;
        TerrainSplatting(const TerrainSplatting&) = delete;
        TerrainSplatting& operator=(const TerrainSplatting&) = delete;

        /// Initialises splat map + texture arrays.
        ///
        /// Config keys read:
        ///   terrain.splat.tiling_grass  (float, default 8.0)  – metres per tile, grass layer
        ///   terrain.splat.tiling_dirt   (float, default 8.0)  – metres per tile, dirt layer
        ///   terrain.splat.tiling_rock   (float, default 16.0) – metres per tile, rock layer
        ///   terrain.splat.tiling_snow   (float, default 12.0) – metres per tile, snow layer
        ///
        /// \param splatmapRelPath  Content-relative path to the splat map image (e.g.
        ///                         "terrain/splatmap.rgba8"). Empty = use default.
        /// \param queue            Graphics queue used for one-time GPU uploads.
        /// \return true on success.
        bool Init(VkDevice device, VkPhysicalDevice physDev,
                  const engine::core::Config& config,
                  const std::string& splatmapRelPath,
                  VkQueue queue, uint32_t queueFamilyIndex);

        /// Destroys all GPU resources. Safe to call when not initialised.
        void Destroy(VkDevice device);

        /// Returns true if Init succeeded and Destroy has not been called.
        bool IsValid() const { return m_splatMap.image != VK_NULL_HANDLE; }

        const SplatMapGpu&    GetSplatMap()    const { return m_splatMap;    }
        const TextureArrayGpu& GetAlbedoArray() const { return m_albedoArray; }
        const TextureArrayGpu& GetNormalArray() const { return m_normalArray; }
        const TextureArrayGpu& GetORMArray()    const { return m_ormArray;    }

        /// Returns the tiling scale (metres per tile) for a given layer index [0, kSplatLayerCount).
        float GetLayerTiling(uint32_t layer) const;

    private:
        SplatMapGpu    m_splatMap;
        TextureArrayGpu m_albedoArray;
        TextureArrayGpu m_normalArray;
        TextureArrayGpu m_ormArray;

        float m_layerTiling[kSplatLayerCount] = { 8.0f, 8.0f, 16.0f, 12.0f };

        // ── Internal upload helpers ───────────────────────────────────────────────

        /// Uploads a flat RGBA8 buffer as a 2D splat map texture.
        static bool UploadSplatMap(VkDevice device, VkPhysicalDevice physDev,
                                   const std::vector<uint8_t>& rgba,
                                   uint32_t width, uint32_t height,
                                   VkQueue queue, uint32_t queueFamilyIndex,
                                   SplatMapGpu& out);

        /// Uploads a flat RGBA8 buffer as a 2D_ARRAY texture with layerCount layers.
        /// The buffer must be exactly width * height * 4 * layerCount bytes,
        /// with layers packed contiguously in memory.
        static bool UploadTextureArray(VkDevice device, VkPhysicalDevice physDev,
                                       const std::vector<uint8_t>& rgba,
                                       uint32_t width, uint32_t height,
                                       uint32_t layerCount,
                                       VkQueue queue, uint32_t queueFamilyIndex,
                                       TextureArrayGpu& out);

        static void DestroySplatMap(VkDevice device, SplatMapGpu& g);
        static void DestroyTextureArray(VkDevice device, TextureArrayGpu& g);
    };

} // namespace engine::render::terrain
