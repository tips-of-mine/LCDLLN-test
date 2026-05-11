#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace engine::render::terrain
{
    /// Binary format for .r16h heightmap files (little-endian):
    ///   magic  : uint32  = 0x504D4148 ("HAMP")
    ///   width  : uint32
    ///   height : uint32
    ///   data   : uint16[width * height]  (row-major, z * width + x)
    static constexpr uint32_t kHeightmapMagic = 0x504D4148u; // 'HAMP'

    /// CPU-side R16 heightmap data.
    struct HeightmapData
    {
        uint32_t width  = 0;
        uint32_t height = 0;
        /// Row-major pixel array, index = z * width + x.
        std::vector<uint16_t> heights;

        /// Returns normalized height [0.0, 1.0] at (x, z). Clamps to bounds.
        float Sample(uint32_t x, uint32_t z) const;

        /// Normalized height [0, 1] with bilinear filtering. \p u and \p v in [0, 1] (edge-clamped).
        float SampleBilinearNorm(float u, float v) const;
    };

    /// GPU R16_UNORM heightmap texture (DEVICE_LOCAL, OPTIMAL tiling).
    struct HeightmapGpu
    {
        VkImage        image   = VK_NULL_HANDLE;
        VkImageView    view    = VK_NULL_HANDLE;
        VkDeviceMemory memory  = VK_NULL_HANDLE;
        VkSampler      sampler = VK_NULL_HANDLE;
        uint32_t       width   = 0;
        uint32_t       height  = 0;
    };

    /// GPU RGBA8_UNORM normal map texture (DEVICE_LOCAL, OPTIMAL tiling).
    /// RGB channels encode the normal as N * 0.5 + 0.5; A = 1.
    struct NormalMapGpu
    {
        VkImage        image   = VK_NULL_HANDLE;
        VkImageView    view    = VK_NULL_HANDLE;
        VkDeviceMemory memory  = VK_NULL_HANDLE;
        VkSampler      sampler = VK_NULL_HANDLE;
    };

    /// Loads R16 heightmaps from disk and uploads them to the GPU as sampled textures.
    /// Uses raw Vulkan allocations (no VMA). Uploads via staging buffer + one-time command buffer.
    class HeightmapLoader
    {
    public:
        HeightmapLoader() = delete;

        /// Loads a heightmap from a .r16h binary file.
        /// \param fullPath  Resolved filesystem path to the file.
        /// \param outData   Populated on success.
        /// \return true on success.
        static bool LoadFromFile(const std::string& fullPath, HeightmapData& outData);

        /// Creates a GPU R16_UNORM texture from CPU data via staging upload.
        /// The resulting image is in SHADER_READ_ONLY_OPTIMAL layout.
        /// \param queue              Graphics queue used for the one-time copy command.
        /// \param queueFamilyIndex   Family index of the graphics queue.
        /// \return true on success.
        static bool UploadHeightmap(VkDevice device, VkPhysicalDevice physDev,
                                    const HeightmapData& data,
                                    VkQueue queue, uint32_t queueFamilyIndex,
                                    HeightmapGpu& outGpu);

        /// Generates a Sobel-filtered normal map from CPU heightmap data and uploads it
        /// as RGBA8_UNORM. The resulting image is in SHADER_READ_ONLY_OPTIMAL layout.
        /// \param heightScale  World units for maximum height (for realistic slope angle).
        /// \param worldScale   World units per heightmap texel (horizontal spacing).
        static bool GenerateAndUploadNormalMap(VkDevice device, VkPhysicalDevice physDev,
                                               const HeightmapData& data,
                                               float heightScale, float worldScale,
                                               VkQueue queue, uint32_t queueFamilyIndex,
                                               NormalMapGpu& outNormal);

        /// Destroys GPU heightmap resources. Safe to call on null handles.
        static void DestroyHeightmap(VkDevice device, HeightmapGpu& gpu);

        /// Destroys GPU normal map resources. Safe to call on null handles.
        static void DestroyNormalMap(VkDevice device, NormalMapGpu& gpu);
    };

} // namespace engine::render::terrain
