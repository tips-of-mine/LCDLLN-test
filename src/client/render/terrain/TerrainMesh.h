#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace engine::render::terrain
{
    /// Number of quads per patch side at LOD 0.
    static constexpr uint32_t kPatchQuads = 16u;
    /// Number of vertices per patch side = kPatchQuads + 1.
    static constexpr uint32_t kPatchVerts = kPatchQuads + 1u; // 17
    /// Total vertices in the shared patch vertex buffer.
    static constexpr uint32_t kPatchVertexCount = kPatchVerts * kPatchVerts; // 289
    /// Number of LOD levels supported.
    static constexpr uint32_t kTerrainLodCount = 5u;

    /// A single terrain vertex: local patch XZ position in [0, kPatchQuads].
    struct TerrainVertex
    {
        float x = 0.0f; ///< Local X in [0, kPatchQuads]
        float z = 0.0f; ///< Local Z in [0, kPatchQuads]
    };

    /// GPU index buffer for one LOD level.
    struct TerrainLodBuffer
    {
        VkBuffer       buffer     = VK_NULL_HANDLE;
        VkDeviceMemory memory     = VK_NULL_HANDLE;
        uint32_t       indexCount = 0;
    };

    /// All GPU mesh resources for a single terrain patch.
    /// One shared vertex buffer (kPatchVertexCount vertices) and kTerrainLodCount index buffers.
    struct TerrainMeshGpu
    {
        VkBuffer         vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory   vertexMemory = VK_NULL_HANDLE;
        uint32_t         vertexCount  = 0;
        TerrainLodBuffer lod[kTerrainLodCount];
    };

    /// Generates the terrain patch mesh on the GPU.
    ///
    /// Vertex buffer: kPatchVertexCount vertices, each a vec2 (local XZ in [0, kPatchQuads]).
    /// Index buffer per LOD: LOD N uses step S = 2^N, resulting in (kPatchQuads/S)^2 quads.
    ///   LOD 0 → 16×16 quads → 1536 uint16 indices
    ///   LOD 1 → 8×8  quads → 384  uint16 indices
    ///   LOD 2 → 4×4  quads → 96   uint16 indices
    ///   LOD 3 → 2×2  quads → 24   uint16 indices
    ///   LOD 4 → 1×1  quad  → 6    uint16 indices
    ///
    /// Uses HOST_VISIBLE | HOST_COHERENT memory (raw Vulkan, no VMA).
    class TerrainMesh
    {
    public:
        TerrainMesh() = delete;

        /// Allocates and fills all GPU buffers for a terrain patch.
        /// \return true on success.
        static bool Generate(VkDevice device, VkPhysicalDevice physDev,
                             TerrainMeshGpu& outMesh);

        /// Releases all GPU mesh resources. Safe on null handles.
        static void Destroy(VkDevice device, TerrainMeshGpu& mesh);
    };

} // namespace engine::render::terrain
