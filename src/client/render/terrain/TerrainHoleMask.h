#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace engine::render::terrain
{
    /// Binary format for .hmask files (little-endian):
    ///   magic  : uint32  = 0x4B534D48 ('HMSK')
    ///   width  : uint32  (number of quads along X = heightmap.width  - 1)
    ///   height : uint32  (number of quads along Z = heightmap.height - 1)
    ///   data   : uint8[width * height]
    ///            0   = hole (transparent, unwalkable)
    ///            255 = solid (opaque, walkable)
    ///
    /// Pixel (qx, qz) addresses quad column qx, row qz (row-major: qz * width + qx).
    static constexpr uint32_t kHoleMaskMagic = 0x4B534D48u; // 'HMSK'

    // ─────────────────────────────────────────────────────────────────────────
    // CPU-side hole mask data
    // ─────────────────────────────────────────────────────────────────────────

    /// CPU-side R8 hole mask.
    struct HoleMaskData
    {
        uint32_t             width  = 0; ///< Number of quads along X
        uint32_t             height = 0; ///< Number of quads along Z
        std::vector<uint8_t> mask;       ///< Row-major, index = qz * width + qx

        /// Returns true if the quad at (qx, qz) is a hole.
        /// Returns false (solid) for out-of-bounds coordinates.
        bool IsHole(uint32_t qx, uint32_t qz) const;

        /// Returns true if any quad in the data is a hole (value == 0).
        bool HasAnyHole() const;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // GPU hole mask texture
    // ─────────────────────────────────────────────────────────────────────────

    /// GPU R8_UNORM hole mask texture (DEVICE_LOCAL, OPTIMAL tiling).
    /// Sampled in the terrain fragment shader at binding 7.
    /// Value 0.0 = hole (fragment discarded), 1.0 = solid (fragment kept).
    struct HoleMaskGpu
    {
        VkImage        image   = VK_NULL_HANDLE;
        VkImageView    view    = VK_NULL_HANDLE;
        VkDeviceMemory memory  = VK_NULL_HANDLE;
        VkSampler      sampler = VK_NULL_HANDLE;
        uint32_t       width   = 0;
        uint32_t       height  = 0;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainHoleMask — static utility class
    // ─────────────────────────────────────────────────────────────────────────

    /// Manages terrain hole mask resources: file loading, GPU upload, and navmesh query.
    ///
    /// If the hole mask file is absent or invalid, a fully-solid fallback mask
    /// is generated (all bytes = 255) so the terrain renders without holes.
    class TerrainHoleMask
    {
    public:
        TerrainHoleMask() = delete;

        /// Loads a hole mask from a .hmask binary file.
        ///
        /// \param fullPath  Resolved filesystem path to the .hmask file.
        /// \param outData   Populated on success.
        /// \return true on success, false if file is missing or malformed.
        static bool LoadFromFile(const std::string& fullPath, HoleMaskData& outData);

        /// Generates a fully-solid fallback mask (all 255) for the given quad grid.
        ///
        /// Used when the hole mask file is absent so the system degrades gracefully.
        /// \param quadW  Width in quads  (heightmap.width  - 1)
        /// \param quadH  Height in quads (heightmap.height - 1)
        static void GenerateSolid(uint32_t quadW, uint32_t quadH, HoleMaskData& outData);

        /// Uploads the CPU hole mask to the GPU as an R8_UNORM sampled texture.
        ///
        /// The texture resolution matches the quad grid (one texel per quad).
        /// Resulting image is in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL.
        /// Uses raw Vulkan allocations (no VMA). Upload via staging buffer +
        /// one-time command buffer.
        ///
        /// \param queue             Graphics queue for the one-time copy.
        /// \param queueFamilyIndex  Family index of the graphics queue.
        /// \return true on success.
        static bool UploadToGpu(VkDevice device, VkPhysicalDevice physDev,
                                 const HoleMaskData& data,
                                 VkQueue queue, uint32_t queueFamilyIndex,
                                 HoleMaskGpu& outGpu);

        /// Destroys all GPU resources. Safe to call on null handles.
        static void DestroyGpu(VkDevice device, HoleMaskGpu& gpu);
    };

} // namespace engine::render::terrain
