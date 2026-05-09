#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace engine::render::terrain
{
    /// Binary format for .cliff files (little-endian):
    ///   magic       : uint32  = 0x46464C43 ('CLFF')
    ///   vertexCount : uint32
    ///   indexCount  : uint32  (must be multiple of 3)
    ///   vertices    : CliffVertex[vertexCount]
    ///   indices     : uint16[indexCount]
    ///
    /// The mesh is in world space. Vertices at the border between cliff and
    /// terrain use blendWeight = 1.0 ("terrain side") so that the edge can
    /// be hidden beneath the heightmap surface. Interior cliff vertices use
    /// blendWeight = 0.0.
    static constexpr uint32_t kCliffMeshMagic = 0x46464C43u; // 'CLFF'

    // ─────────────────────────────────────────────────────────────────────────
    // Cliff vertex format
    // ─────────────────────────────────────────────────────────────────────────

    /// A single cliff mesh vertex (36 bytes).
    ///
    /// Layout (used by terrain_cliff.vert):
    ///   location 0: vec3 position    — world-space XYZ
    ///   location 1: vec3 normal      — world-space vertex normal (unit)
    ///   location 2: vec2 uv          — texture UV [0,1]
    ///   location 3: float blendWeight — 0=cliff interior, 1=terrain-blended edge
    struct CliffVertex
    {
        float position[3]  = { 0.f, 0.f, 0.f }; ///< World-space position
        float normal[3]    = { 0.f, 1.f, 0.f }; ///< World-space normal (unit)
        float uv[2]        = { 0.f, 0.f };       ///< Texture UV
        float blendWeight  = 0.f;                 ///< 0=cliff, 1=terrain-edge blend
    };
    static_assert(sizeof(CliffVertex) == 36u, "CliffVertex must be 36 bytes");

    // ─────────────────────────────────────────────────────────────────────────
    // CPU cliff mesh data
    // ─────────────────────────────────────────────────────────────────────────

    /// CPU-side cliff mesh data (after loading from a .cliff file).
    struct CliffMeshData
    {
        std::vector<CliffVertex> vertices;
        std::vector<uint16_t>    indices;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // GPU cliff mesh resources
    // ─────────────────────────────────────────────────────────────────────────

    /// GPU resources for one cliff mesh instance.
    struct CliffMeshGpu
    {
        VkBuffer       vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer       indexBuffer  = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory  = VK_NULL_HANDLE;
        uint32_t       vertexCount  = 0u;
        uint32_t       indexCount   = 0u;
    };

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainCliffMesh — static utility class
    // ─────────────────────────────────────────────────────────────────────────

    /// Loads and uploads manually-authored cliff mesh geometry.
    ///
    /// Cliff meshes are placed in world space and rendered into the same GBuffer
    /// as terrain (using the terrain_cliff.vert / terrain_cliff.frag shaders).
    /// Each cliff mesh file corresponds to one placed cliff section.
    ///
    /// The "blend edge" pattern is encoded in blendWeight:
    ///   - Vertices at the junction between cliff and heightmap terrain have
    ///     blendWeight = 1.0, indicating they should be snapped to the heightmap
    ///     surface during rendering or dissolved into it.
    ///   - Interior cliff vertices have blendWeight = 0.0.
    class TerrainCliffMesh
    {
    public:
        TerrainCliffMesh() = delete;

        /// Loads a cliff mesh from a .cliff binary file.
        ///
        /// \param fullPath  Resolved filesystem path to the .cliff file.
        /// \param outData   Populated on success.
        /// \return true on success, false if file is missing or malformed.
        static bool LoadFromFile(const std::string& fullPath, CliffMeshData& outData);

        /// Uploads a CPU cliff mesh to the GPU.
        ///
        /// Both vertex and index buffers are allocated as HOST_VISIBLE | HOST_COHERENT
        /// (raw Vulkan, no VMA). No staging upload needed for small cliff meshes.
        ///
        /// \return true on success.
        static bool UploadToGpu(VkDevice device, VkPhysicalDevice physDev,
                                 const CliffMeshData& data,
                                 CliffMeshGpu& outGpu);

        /// Destroys all GPU resources for a cliff mesh. Safe on null handles.
        static void DestroyGpu(VkDevice device, CliffMeshGpu& gpu);
    };

} // namespace engine::render::terrain
