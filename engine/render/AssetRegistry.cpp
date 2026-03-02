#include "engine/render/AssetRegistry.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <vulkan/vulkan_core.h>

#include <cstring>
#include <algorithm>

namespace engine::render
{
	// --- Mesh/Texture binary format (no external deps) ---
	// .mesh: magic "MESH" (4), version (4), numVertices (4), numIndices (4), vertices (numVertices*32), indices (numIndices*4)
	//        vertex = position float3, normal float3, uv float2 = 32 bytes
	// .texr: magic "TEXR" (4), width (4), height (4), sRGB (4), pixels (width*height*4) RGBA

	static constexpr uint32_t kMeshMagic = 0x4D455348u; // "MESH"
	static constexpr uint32_t kTexrMagic = 0x54455852u; // "TEXR"
	static constexpr size_t kMeshVertexStride = 32u;    // 3+3+2 floats

	namespace
	{
		uint32_t findMemoryTypeImpl(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
		{
			VkPhysicalDeviceMemoryProperties memProps;
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
			for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
			{
				if ((typeFilter & (1u << i)) != 0
					&& (memProps.memoryTypes[i].propertyFlags & properties) == properties)
					return i;
			}
			return UINT32_MAX;
		}
	}

	// --- Handles ---

	bool MeshHandle::IsValid() const
	{
		return m_registry && m_id != kInvalidAssetId && m_registry->GetMesh(m_id) != nullptr;
	}

	MeshAsset* MeshHandle::Get() const
	{
		return m_registry ? m_registry->GetMesh(m_id) : nullptr;
	}

	bool TextureHandle::IsValid() const
	{
		return m_registry && m_id != kInvalidAssetId && m_registry->GetTexture(m_id) != nullptr;
	}

	TextureAsset* TextureHandle::Get() const
	{
		return m_registry ? m_registry->GetTexture(m_id) : nullptr;
	}

	// --- AssetRegistry ---

	void AssetRegistry::Init(VkDevice device, VkPhysicalDevice physicalDevice, const engine::core::Config& config)
	{
		m_device = device;
		m_physicalDevice = physicalDevice;
		m_config = &config;
	}

	uint32_t AssetRegistry::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
	{
		return findMemoryTypeImpl(m_physicalDevice, typeFilter, properties);
	}

	MeshHandle AssetRegistry::LoadMesh(std::string_view relativePath)
	{
		std::string key(relativePath);
		auto it = m_meshPathToId.find(key);
		if (it != m_meshPathToId.end())
			return MeshHandle(this, it->second);
		AssetId id = loadMeshInternal(relativePath);
		if (id != kInvalidAssetId)
			m_meshPathToId[key] = id;
		return MeshHandle(this, id);
	}

	TextureHandle AssetRegistry::LoadTexture(std::string_view relativePath, bool useSrgb)
	{
		std::string key(relativePath);
		if (useSrgb) key += ":srgb";
		else key += ":lin";
		auto it = m_texturePathToId.find(key);
		if (it != m_texturePathToId.end())
			return TextureHandle(this, it->second);
		AssetId id = loadTextureInternal(relativePath, useSrgb);
		if (id != kInvalidAssetId)
			m_texturePathToId[key] = id;
		return TextureHandle(this, id);
	}

	MeshAsset* AssetRegistry::GetMesh(AssetId id) const
	{
		auto it = m_meshes.find(id);
		return it != m_meshes.end() ? const_cast<MeshAsset*>(&it->second) : nullptr;
	}

	TextureAsset* AssetRegistry::GetTexture(AssetId id) const
	{
		auto it = m_textures.find(id);
		return it != m_textures.end() ? const_cast<TextureAsset*>(&it->second) : nullptr;
	}

	void AssetRegistry::Destroy()
	{
		if (m_device == VK_NULL_HANDLE) return;
		for (auto& p : m_meshes)
		{
			if (p.second.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, p.second.vertexBuffer, nullptr);
			if (p.second.indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(m_device, p.second.indexBuffer, nullptr);
			if (p.second.vertexMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, p.second.vertexMemory, nullptr);
			if (p.second.indexMemory != VK_NULL_HANDLE) vkFreeMemory(m_device, p.second.indexMemory, nullptr);
		}
		m_meshes.clear();
		m_meshPathToId.clear();
		for (auto& p : m_textures)
		{
			if (p.second.view != VK_NULL_HANDLE) vkDestroyImageView(m_device, p.second.view, nullptr);
			if (p.second.image != VK_NULL_HANDLE) vkDestroyImage(m_device, p.second.image, nullptr);
			if (p.second.memory != VK_NULL_HANDLE) vkFreeMemory(m_device, p.second.memory, nullptr);
		}
		m_textures.clear();
		m_texturePathToId.clear();
		m_device = VK_NULL_HANDLE;
		m_physicalDevice = VK_NULL_HANDLE;
		m_config = nullptr;
		m_nextMeshId = 1;
		m_nextTextureId = 1;
	}

	AssetId AssetRegistry::loadMeshInternal(std::string_view relativePath)
	{
		if (!m_config || m_device == VK_NULL_HANDLE) return kInvalidAssetId;
		std::vector<uint8_t> data = engine::platform::FileSystem::ReadAllBytesContent(*m_config, relativePath);
		if (data.size() < 16) { LOG_ERROR(Render, "AssetRegistry: mesh file too small: {}", relativePath); return kInvalidAssetId; }
		uint32_t magic = 0, version = 0, numVertices = 0, numIndices = 0;
		memcpy(&magic, data.data(), 4);
		memcpy(&version, data.data() + 4, 4);
		memcpy(&numVertices, data.data() + 8, 4);
		memcpy(&numIndices, data.data() + 12, 4);
		if (magic != kMeshMagic || version != 1) { LOG_ERROR(Render, "AssetRegistry: invalid mesh format: {}", relativePath); return kInvalidAssetId; }
		size_t vertexBytes = numVertices * kMeshVertexStride;
		size_t indexBytes = numIndices * sizeof(uint32_t);
		if (data.size() < 16 + vertexBytes + indexBytes) { LOG_ERROR(Render, "AssetRegistry: mesh file truncated: {}", relativePath); return kInvalidAssetId; }
		const uint8_t* vertexData = data.data() + 16;
		const uint8_t* indexData = data.data() + 16 + vertexBytes;

		MeshAsset asset{};
		asset.vertexCount = numVertices;
		asset.indexCount = numIndices;

		// Host-visible coherent memory so we can map and copy directly (no staging queue for MVP).
		VkBufferCreateInfo bufInfo{};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = vertexBytes;
		bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateBuffer(m_device, &bufInfo, nullptr, &asset.vertexBuffer) != VK_SUCCESS) return kInvalidAssetId;
		bufInfo.size = indexBytes;
		bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		if (vkCreateBuffer(m_device, &bufInfo, nullptr, &asset.indexBuffer) != VK_SUCCESS) { vkDestroyBuffer(m_device, asset.vertexBuffer, nullptr); return kInvalidAssetId; }
		VkMemoryRequirements vr, ir;
		vkGetBufferMemoryRequirements(m_device, asset.vertexBuffer, &vr);
		vkGetBufferMemoryRequirements(m_device, asset.indexBuffer, &ir);
		uint32_t vMemType = findMemoryType(vr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		uint32_t iMemType = findMemoryType(ir.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (vMemType == UINT32_MAX || iMemType == UINT32_MAX) { vkDestroyBuffer(m_device, asset.indexBuffer, nullptr); vkDestroyBuffer(m_device, asset.vertexBuffer, nullptr); LOG_ERROR(Render, "AssetRegistry: no host-visible memory for mesh"); return kInvalidAssetId; }
		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = vr.size;
		allocInfo.memoryTypeIndex = vMemType;
		if (vkAllocateMemory(m_device, &allocInfo, nullptr, &asset.vertexMemory) != VK_SUCCESS) { vkDestroyBuffer(m_device, asset.indexBuffer, nullptr); vkDestroyBuffer(m_device, asset.vertexBuffer, nullptr); return kInvalidAssetId; }
		allocInfo.allocationSize = ir.size;
		allocInfo.memoryTypeIndex = iMemType;
		if (vkAllocateMemory(m_device, &allocInfo, nullptr, &asset.indexMemory) != VK_SUCCESS) { vkFreeMemory(m_device, asset.vertexMemory, nullptr); vkDestroyBuffer(m_device, asset.indexBuffer, nullptr); vkDestroyBuffer(m_device, asset.vertexBuffer, nullptr); return kInvalidAssetId; }
		vkBindBufferMemory(m_device, asset.vertexBuffer, asset.vertexMemory, 0);
		vkBindBufferMemory(m_device, asset.indexBuffer, asset.indexMemory, 0);
		void* pv = nullptr, *pi = nullptr;
		vkMapMemory(m_device, asset.vertexMemory, 0, vertexBytes, 0, &pv);
		memcpy(pv, vertexData, vertexBytes);
		vkUnmapMemory(m_device, asset.vertexMemory);
		vkMapMemory(m_device, asset.indexMemory, 0, indexBytes, 0, &pi);
		memcpy(pi, indexData, indexBytes);
		vkUnmapMemory(m_device, asset.indexMemory);
		AssetId id = m_nextMeshId++;
		m_meshes[id] = std::move(asset);
		LOG_INFO(Render, "AssetRegistry: loaded mesh {} ({} vertices, {} indices)", relativePath, numVertices, numIndices);
		return id;
	}

	AssetId AssetRegistry::loadTextureInternal(std::string_view relativePath, bool useSrgb)
	{
		if (!m_config || m_device == VK_NULL_HANDLE) return kInvalidAssetId;
		std::vector<uint8_t> data = engine::platform::FileSystem::ReadAllBytesContent(*m_config, relativePath);
		if (data.size() < 16) { LOG_ERROR(Render, "AssetRegistry: texture file too small: {}", relativePath); return kInvalidAssetId; }
		uint32_t magic = 0, width = 0, height = 0, srgbFlag = 0;
		memcpy(&magic, data.data(), 4);
		memcpy(&width, data.data() + 4, 4);
		memcpy(&height, data.data() + 8, 4);
		memcpy(&srgbFlag, data.data() + 12, 4);
		if (magic != kTexrMagic || width == 0 || height == 0) { LOG_ERROR(Render, "AssetRegistry: invalid texture format: {}", relativePath); return kInvalidAssetId; }
		size_t pixelBytes = static_cast<size_t>(width) * height * 4;
		if (data.size() < 16 + pixelBytes) { LOG_ERROR(Render, "AssetRegistry: texture file truncated: {}", relativePath); return kInvalidAssetId; }
		const uint8_t* pixels = data.data() + 16;

		TextureAsset asset{};
		asset.width = width;
		asset.height = height;
		VkFormat format = useSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		VkImageCreateInfo imgInfo{};
		imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imgInfo.imageType = VK_IMAGE_TYPE_2D;
		imgInfo.format = format;
		imgInfo.extent = { width, height, 1 };
		imgInfo.mipLevels = 1;
		imgInfo.arrayLayers = 1;
		imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imgInfo.tiling = VK_IMAGE_TILING_LINEAR;
		imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		imgInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		if (vkCreateImage(m_device, &imgInfo, nullptr, &asset.image) != VK_SUCCESS) return kInvalidAssetId;
		VkMemoryRequirements mr;
		vkGetImageMemoryRequirements(m_device, asset.image, &mr);
		uint32_t memType = findMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memType == UINT32_MAX) { vkDestroyImage(m_device, asset.image, nullptr); return kInvalidAssetId; }
		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = mr.size;
		allocInfo.memoryTypeIndex = memType;
		if (vkAllocateMemory(m_device, &allocInfo, nullptr, &asset.memory) != VK_SUCCESS) { vkDestroyImage(m_device, asset.image, nullptr); return kInvalidAssetId; }
		vkBindImageMemory(m_device, asset.image, asset.memory, 0);
		void* ptr = nullptr;
		VkImageSubresource subres{};
		subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subres.mipLevel = 0;
		subres.arrayLayer = 0;
		VkSubresourceLayout layout{};
		vkGetImageSubresourceLayout(m_device, asset.image, &subres, &layout);
		vkMapMemory(m_device, asset.memory, 0, mr.size, 0, &ptr);
		const uint8_t* src = pixels;
		uint8_t* dst = static_cast<uint8_t*>(ptr);
		for (uint32_t y = 0; y < height; ++y)
		{
			memcpy(dst + y * layout.rowPitch, src + y * width * 4, width * 4);
		}
		vkUnmapMemory(m_device, asset.memory);
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = asset.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		if (vkCreateImageView(m_device, &viewInfo, nullptr, &asset.view) != VK_SUCCESS) { vkFreeMemory(m_device, asset.memory, nullptr); vkDestroyImage(m_device, asset.image, nullptr); return kInvalidAssetId; }
		AssetId id = m_nextTextureId++;
		m_textures[id] = std::move(asset);
		LOG_INFO(Render, "AssetRegistry: loaded texture {} ({}x{}, {})", relativePath, width, height, useSrgb ? "sRGB" : "linear");
		return id;
	}
}
