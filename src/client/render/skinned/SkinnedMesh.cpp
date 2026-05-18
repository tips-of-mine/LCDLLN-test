#include "src/client/render/skinned/SkinnedMesh.h"

#include "src/shared/core/Log.h"

#include <cstring>

namespace engine::render::skinned
{

namespace
{
    /// Cherche un memory type compatible avec `typeBits` (bitmask renvoyé par
    /// vkGetBufferMemoryRequirements) et qui contient au moins toutes les
    /// propriétés `wanted` (typiquement HOST_VISIBLE | HOST_COHERENT).
    /// \return Index dans VkPhysicalDeviceMemoryProperties::memoryTypes, ou
    ///         UINT32_MAX si aucun type n'est compatible (erreur — le caller
    ///         doit traiter ça comme un échec d'allocation).
    uint32_t FindMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags wanted)
    {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & wanted) == wanted)
                return i;
        }
        return UINT32_MAX;
    }

    /// Crée un VkBuffer + VkDeviceMemory host-visible/coherent, et y copie
    /// `bytes` octets depuis `src` via map/unmap. Tout est nettoyé sur erreur
    /// (aucune fuite, outBuf/outMem remis à VK_NULL_HANDLE).
    ///
    /// \param usage Flag d'usage du buffer (VERTEX_BUFFER_BIT, INDEX_BUFFER_BIT, ...).
    /// \param src   Pointeur vers les données source. bytes==0 est traité comme
    ///              succès trivial (mais ici on garde le path nominal — appelé
    ///              uniquement avec bytes > 0 pour un mesh valide).
    /// \return false si vkCreateBuffer / vkAllocateMemory / vkBindBufferMemory /
    ///         vkMapMemory échoue, ou si aucun memory type compatible existe.
    bool CreateHostVisibleBuffer(VkDevice device, VkPhysicalDevice phys,
                                 VkBufferUsageFlags usage,
                                 const void* src, size_t bytes,
                                 VkBuffer* outBuf, VkDeviceMemory* outMem)
    {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = bytes;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, outBuf) != VK_SUCCESS) return false;

        VkMemoryRequirements mr{};
        vkGetBufferMemoryRequirements(device, *outBuf, &mr);
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = mr.size;
        ai.memoryTypeIndex = FindMemoryType(phys, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) {
            vkDestroyBuffer(device, *outBuf, nullptr);
            *outBuf = VK_NULL_HANDLE;
            return false;
        }

        if (vkAllocateMemory(device, &ai, nullptr, outMem) != VK_SUCCESS) {
            vkDestroyBuffer(device, *outBuf, nullptr);
            *outBuf = VK_NULL_HANDLE;
            return false;
        }
        if (vkBindBufferMemory(device, *outBuf, *outMem, 0) != VK_SUCCESS) {
            vkFreeMemory(device, *outMem, nullptr);
            vkDestroyBuffer(device, *outBuf, nullptr);
            *outBuf = VK_NULL_HANDLE;
            *outMem = VK_NULL_HANDLE;
            return false;
        }

        void* mapped = nullptr;
        if (vkMapMemory(device, *outMem, 0, bytes, 0, &mapped) != VK_SUCCESS) {
            vkFreeMemory(device, *outMem, nullptr);
            vkDestroyBuffer(device, *outBuf, nullptr);
            *outBuf = VK_NULL_HANDLE;
            *outMem = VK_NULL_HANDLE;
            return false;
        }
        std::memcpy(mapped, src, bytes);
        vkUnmapMemory(device, *outMem);
        return true;
    }
}

bool SkinnedMesh::Upload(VkDevice device, VkPhysicalDevice physicalDevice, const SkinnedMeshCpuData& cpu)
{
    skeleton = cpu.skeleton;
    clips = cpu.clips;
    indexCount = static_cast<uint32_t>(cpu.indices.size());

    const size_t vBytes = cpu.vertices.size() * sizeof(SkinnedVertex);
    const size_t iBytes = cpu.indices.size() * sizeof(uint32_t);

    if (!CreateHostVisibleBuffer(device, physicalDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  cpu.vertices.data(), vBytes, &vertexBuffer, &vertexMemory)) {
        LOG_ERROR(Render, "[SkinnedMesh] vertex buffer creation failed ({} bytes)", vBytes);
        return false;
    }
    if (!CreateHostVisibleBuffer(device, physicalDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  cpu.indices.data(), iBytes, &indexBuffer, &indexMemory)) {
        LOG_ERROR(Render, "[SkinnedMesh] index buffer creation failed ({} bytes)", iBytes);
        // Nettoie le vertex buffer déjà alloué pour éviter une fuite.
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexMemory, nullptr);
        vertexBuffer = VK_NULL_HANDLE;
        vertexMemory = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void SkinnedMesh::Destroy(VkDevice device)
{
    if (vertexBuffer) { vkDestroyBuffer(device, vertexBuffer, nullptr); vertexBuffer = VK_NULL_HANDLE; }
    if (vertexMemory) { vkFreeMemory(device, vertexMemory, nullptr); vertexMemory = VK_NULL_HANDLE; }
    if (indexBuffer)  { vkDestroyBuffer(device, indexBuffer, nullptr);  indexBuffer = VK_NULL_HANDLE; }
    if (indexMemory)  { vkFreeMemory(device, indexMemory, nullptr);     indexMemory = VK_NULL_HANDLE; }
}

const AnimationClip* SkinnedMesh::FindClip(const std::string& name) const
{
    for (const auto& c : clips) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

}  // namespace engine::render::skinned
