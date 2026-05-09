#include "engine/render/terrain/TerrainMesh.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cstring>
#include <vector>

namespace engine::render::terrain
{
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

        /// Allocates a HOST_VISIBLE|HOST_COHERENT buffer and copies data into it.
        bool CreateAndFillBuffer(VkDevice device, VkPhysicalDevice physDev,
                                 VkDeviceSize size, VkBufferUsageFlags usage,
                                 const void* srcData,
                                 VkBuffer& outBuffer, VkDeviceMemory& outMemory)
        {
            VkBufferCreateInfo bi{};
            bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bi.size        = size;
            bi.usage       = usage;
            bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bi, nullptr, &outBuffer) != VK_SUCCESS ||
                outBuffer == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainMesh] vkCreateBuffer failed (size={} usage=0x{:x})",
                          size, usage);
                return false;
            }

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, outBuffer, &req);

            const uint32_t memType = FindMemType(physDev, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (memType == UINT32_MAX)
            {
                LOG_ERROR(Render, "[TerrainMesh] No HOST_VISIBLE memory type available");
                vkDestroyBuffer(device, outBuffer, nullptr);
                outBuffer = VK_NULL_HANDLE;
                return false;
            }

            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = memType;

            if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS ||
                outMemory == VK_NULL_HANDLE)
            {
                LOG_ERROR(Render, "[TerrainMesh] vkAllocateMemory failed");
                vkDestroyBuffer(device, outBuffer, nullptr);
                outBuffer = VK_NULL_HANDLE;
                return false;
            }

            if (vkBindBufferMemory(device, outBuffer, outMemory, 0) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainMesh] vkBindBufferMemory failed");
                vkFreeMemory(device, outMemory, nullptr);
                vkDestroyBuffer(device, outBuffer, nullptr);
                outBuffer = VK_NULL_HANDLE;
                outMemory = VK_NULL_HANDLE;
                return false;
            }

            void* mapped = nullptr;
            if (vkMapMemory(device, outMemory, 0, size, 0, &mapped) != VK_SUCCESS)
            {
                LOG_ERROR(Render, "[TerrainMesh] vkMapMemory failed");
                vkFreeMemory(device, outMemory, nullptr);
                vkDestroyBuffer(device, outBuffer, nullptr);
                outBuffer = VK_NULL_HANDLE;
                outMemory = VK_NULL_HANDLE;
                return false;
            }
            std::memcpy(mapped, srcData, static_cast<size_t>(size));
            vkUnmapMemory(device, outMemory);
            return true;
        }
    } // namespace

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainMesh::Generate
    // ─────────────────────────────────────────────────────────────────────────────

    bool TerrainMesh::Generate(VkDevice device, VkPhysicalDevice physDev,
                               TerrainMeshGpu& outMesh)
    {
        LOG_INFO(Render, "[TerrainMesh] Generating patch mesh ({} vertices, {} LODs)",
                 kPatchVertexCount, kTerrainLodCount);

        if (device == VK_NULL_HANDLE || physDev == VK_NULL_HANDLE)
        {
            LOG_ERROR(Render, "[TerrainMesh] Generate: invalid device");
            return false;
        }

        // ── Vertex buffer: kPatchVerts × kPatchVerts vec2 positions ──────────────
        // Each vertex stores its raw local XZ position in [0, kPatchQuads].
        std::vector<TerrainVertex> vertices(kPatchVertexCount);
        for (uint32_t z = 0; z < kPatchVerts; ++z)
        {
            for (uint32_t x = 0; x < kPatchVerts; ++x)
            {
                const uint32_t idx = z * kPatchVerts + x;
                vertices[idx].x = static_cast<float>(x);
                vertices[idx].z = static_cast<float>(z);
            }
        }

        const VkDeviceSize vertexBytes =
            static_cast<VkDeviceSize>(kPatchVertexCount) * sizeof(TerrainVertex);

        if (!CreateAndFillBuffer(device, physDev, vertexBytes,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 vertices.data(),
                                 outMesh.vertexBuffer, outMesh.vertexMemory))
        {
            LOG_ERROR(Render, "[TerrainMesh] Failed to create vertex buffer");
            return false;
        }
        outMesh.vertexCount = kPatchVertexCount;
        LOG_DEBUG(Render, "[TerrainMesh] Vertex buffer OK ({} bytes)", vertexBytes);

        // ── Index buffers: one per LOD ────────────────────────────────────────────
        // LOD N uses step S = 2^N.  Only vertices at (x,z) where x%S==0 && z%S==0 are used.
        // Winding order: CCW when viewed from above (Y-up, camera looking down).
        // Vertex at (x, z) in the 17×17 grid → index = x + z × kPatchVerts.
        for (uint32_t lod = 0; lod < kTerrainLodCount; ++lod)
        {
            const uint32_t step    = (1u << lod); // 1, 2, 4, 8, 16
            const uint32_t quads   = kPatchQuads / step;
            const uint32_t triIdx  = quads * quads * 6u;

            std::vector<uint16_t> indices(triIdx);
            uint32_t k = 0;
            for (uint32_t qz = 0; qz < quads; ++qz)
            {
                for (uint32_t qx = 0; qx < quads; ++qx)
                {
                    // Four corners of the quad (in the 17×17 grid)
                    const uint16_t bl = static_cast<uint16_t>( qx      * step + (qz      * step) * kPatchVerts);
                    const uint16_t br = static_cast<uint16_t>((qx + 1) * step + (qz      * step) * kPatchVerts);
                    const uint16_t tl = static_cast<uint16_t>( qx      * step + ((qz + 1) * step) * kPatchVerts);
                    const uint16_t tr = static_cast<uint16_t>((qx + 1) * step + ((qz + 1) * step) * kPatchVerts);

                    // Two triangles (CCW from above)
                    indices[k++] = bl; indices[k++] = tl; indices[k++] = tr;
                    indices[k++] = bl; indices[k++] = tr; indices[k++] = br;
                }
            }

            const VkDeviceSize indexBytes =
                static_cast<VkDeviceSize>(triIdx) * sizeof(uint16_t);

            if (!CreateAndFillBuffer(device, physDev, indexBytes,
                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                     indices.data(),
                                     outMesh.lod[lod].buffer, outMesh.lod[lod].memory))
            {
                LOG_ERROR(Render, "[TerrainMesh] Failed to create LOD {} index buffer", lod);
                Destroy(device, outMesh);
                return false;
            }
            outMesh.lod[lod].indexCount = triIdx;
            LOG_DEBUG(Render, "[TerrainMesh] LOD {} index buffer OK (step={} quads={}×{} indices={})",
                      lod, step, quads, quads, triIdx);
        }

        LOG_INFO(Render, "[TerrainMesh] Generate OK");
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────────
    // TerrainMesh::Destroy
    // ─────────────────────────────────────────────────────────────────────────────

    void TerrainMesh::Destroy(VkDevice device, TerrainMeshGpu& mesh)
    {
        for (uint32_t lod = 0; lod < kTerrainLodCount; ++lod)
        {
            if (mesh.lod[lod].buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(device, mesh.lod[lod].buffer, nullptr);
                mesh.lod[lod].buffer = VK_NULL_HANDLE;
            }
            if (mesh.lod[lod].memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, mesh.lod[lod].memory, nullptr);
                mesh.lod[lod].memory = VK_NULL_HANDLE;
            }
            mesh.lod[lod].indexCount = 0;
        }
        if (mesh.vertexBuffer != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
            mesh.vertexBuffer = VK_NULL_HANDLE;
        }
        if (mesh.vertexMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(device, mesh.vertexMemory, nullptr);
            mesh.vertexMemory = VK_NULL_HANDLE;
        }
        mesh.vertexCount = 0;
        LOG_INFO(Render, "[TerrainMesh] Destroyed");
    }

} // namespace engine::render::terrain
