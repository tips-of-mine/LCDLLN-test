#include "engine/render/terrain/TerrainCliffMesh.h"
#include "engine/core/Log.h"

#include <vulkan/vulkan_core.h>

#include <cstring>
#include <fstream>

namespace engine::render::terrain
{
    // ─────────────────────────────────────────────────────────────────────────
    // Internal helpers
    // ─────────────────────────────────────────────────────────────────────────
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

        /// Allocates a HOST_VISIBLE | HOST_COHERENT buffer, copies data into it.
        /// Returns true on success; sets buf/mem to VK_NULL_HANDLE on failure.
        bool AllocAndFill(VkDevice device, VkPhysicalDevice physDev,
                          VkBufferUsageFlags usage,
                          const void* src, VkDeviceSize size,
                          VkBuffer& outBuf, VkDeviceMemory& outMem)
        {
            VkBufferCreateInfo bci{};
            bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bci.size        = size;
            bci.usage       = usage;
            bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            if (vkCreateBuffer(device, &bci, nullptr, &outBuf) != VK_SUCCESS)
                return false;

            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, outBuf, &req);

            const uint32_t mt = FindMemType(physDev, req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (mt == UINT32_MAX)
            {
                vkDestroyBuffer(device, outBuf, nullptr);
                outBuf = VK_NULL_HANDLE;
                return false;
            }

            VkMemoryAllocateInfo ai{};
            ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize  = req.size;
            ai.memoryTypeIndex = mt;

            if (vkAllocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS)
            {
                vkDestroyBuffer(device, outBuf, nullptr);
                outBuf = VK_NULL_HANDLE;
                return false;
            }

            vkBindBufferMemory(device, outBuf, outMem, 0);

            void* mapped = nullptr;
            vkMapMemory(device, outMem, 0, size, 0, &mapped);
            std::memcpy(mapped, src, static_cast<size_t>(size));
            vkUnmapMemory(device, outMem);

            return true;
        }
    } // namespace

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainCliffMesh::LoadFromFile
    // ─────────────────────────────────────────────────────────────────────────

    bool TerrainCliffMesh::LoadFromFile(const std::string& fullPath, CliffMeshData& outData)
    {
        std::ifstream fs(fullPath, std::ios::binary);
        if (!fs.is_open())
        {
            LOG_WARN(Render, "[TerrainCliffMesh] File not found: '{}'", fullPath);
            return false;
        }

        uint32_t magic       = 0u;
        uint32_t vertexCount = 0u;
        uint32_t indexCount  = 0u;
        fs.read(reinterpret_cast<char*>(&magic),       sizeof(magic));
        fs.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
        fs.read(reinterpret_cast<char*>(&indexCount),  sizeof(indexCount));

        if (!fs || magic != kCliffMeshMagic)
        {
            LOG_WARN(Render, "[TerrainCliffMesh] Invalid magic in '{}' (got 0x{:08X}, expected 0x{:08X})",
                     fullPath, magic, kCliffMeshMagic);
            return false;
        }

        if (vertexCount == 0u || indexCount == 0u)
        {
            LOG_WARN(Render, "[TerrainCliffMesh] Empty mesh in '{}'", fullPath);
            return false;
        }

        if (indexCount % 3u != 0u)
        {
            LOG_WARN(Render, "[TerrainCliffMesh] indexCount {} is not a multiple of 3 in '{}'",
                     indexCount, fullPath);
            return false;
        }

        outData.vertices.resize(vertexCount);
        fs.read(reinterpret_cast<char*>(outData.vertices.data()),
                static_cast<std::streamsize>(vertexCount * sizeof(CliffVertex)));

        outData.indices.resize(indexCount);
        fs.read(reinterpret_cast<char*>(outData.indices.data()),
                static_cast<std::streamsize>(indexCount * sizeof(uint16_t)));

        if (!fs)
        {
            LOG_WARN(Render, "[TerrainCliffMesh] Truncated data in '{}'", fullPath);
            outData.vertices.clear();
            outData.indices.clear();
            return false;
        }

        LOG_INFO(Render, "[TerrainCliffMesh] Loaded '{}' ({} verts, {} tris)",
                 fullPath, vertexCount, indexCount / 3u);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainCliffMesh::UploadToGpu
    // ─────────────────────────────────────────────────────────────────────────

    bool TerrainCliffMesh::UploadToGpu(VkDevice device, VkPhysicalDevice physDev,
                                        const CliffMeshData& data,
                                        CliffMeshGpu& outGpu)
    {
        if (data.vertices.empty() || data.indices.empty())
        {
            LOG_ERROR(Render, "[TerrainCliffMesh] UploadToGpu: empty mesh data");
            return false;
        }

        const VkDeviceSize vbSize = static_cast<VkDeviceSize>(data.vertices.size()) * sizeof(CliffVertex);
        const VkDeviceSize ibSize = static_cast<VkDeviceSize>(data.indices.size())  * sizeof(uint16_t);

        if (!AllocAndFill(device, physDev, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          data.vertices.data(), vbSize,
                          outGpu.vertexBuffer, outGpu.vertexMemory))
        {
            LOG_ERROR(Render, "[TerrainCliffMesh] Failed to allocate vertex buffer");
            return false;
        }

        if (!AllocAndFill(device, physDev, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          data.indices.data(), ibSize,
                          outGpu.indexBuffer, outGpu.indexMemory))
        {
            LOG_ERROR(Render, "[TerrainCliffMesh] Failed to allocate index buffer");
            // Clean up vertex buffer allocated above
            vkDestroyBuffer(device, outGpu.vertexBuffer, nullptr);
            vkFreeMemory(device, outGpu.vertexMemory, nullptr);
            outGpu.vertexBuffer = VK_NULL_HANDLE;
            outGpu.vertexMemory = VK_NULL_HANDLE;
            return false;
        }

        outGpu.vertexCount = static_cast<uint32_t>(data.vertices.size());
        outGpu.indexCount  = static_cast<uint32_t>(data.indices.size());

        LOG_INFO(Render, "[TerrainCliffMesh] GPU upload OK ({} verts, {} tris)",
                 outGpu.vertexCount, outGpu.indexCount / 3u);
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // TerrainCliffMesh::DestroyGpu
    // ─────────────────────────────────────────────────────────────────────────

    void TerrainCliffMesh::DestroyGpu(VkDevice device, CliffMeshGpu& gpu)
    {
        if (gpu.indexBuffer  != VK_NULL_HANDLE) { vkDestroyBuffer(device, gpu.indexBuffer,  nullptr); gpu.indexBuffer  = VK_NULL_HANDLE; }
        if (gpu.indexMemory  != VK_NULL_HANDLE) { vkFreeMemory(device, gpu.indexMemory,  nullptr);    gpu.indexMemory  = VK_NULL_HANDLE; }
        if (gpu.vertexBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, gpu.vertexBuffer, nullptr); gpu.vertexBuffer = VK_NULL_HANDLE; }
        if (gpu.vertexMemory != VK_NULL_HANDLE) { vkFreeMemory(device, gpu.vertexMemory, nullptr);    gpu.vertexMemory = VK_NULL_HANDLE; }
        gpu.vertexCount = 0u;
        gpu.indexCount  = 0u;
        LOG_INFO(Render, "[TerrainCliffMesh] GPU resources destroyed");
    }

} // namespace engine::render::terrain
