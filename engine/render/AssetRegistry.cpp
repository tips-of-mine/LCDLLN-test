#include "engine/render/AssetRegistry.h"
#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace engine::render
{
	// --- Mesh/Texture binary format (no external deps) ---
	// .mesh: magic bytes "MESH" (4), version (4), numVertices (4), numIndices (4), vertices (numVertices*32), indices (numIndices*4)
	//        vertex = position float3, normal float3, uv float2 = 32 bytes
	// .texr: magic bytes "TEXR" (4), width (4), height (4), sRGB (4), pixels (width*height*4) RGBA

	// Values as read into a uint32_t on little-endian platforms from ASCII byte headers.
	static constexpr uint32_t kMeshMagic = 0x4853454Du; // bytes "MESH"
	static constexpr uint32_t kTexrMagic = 0x52584554u; // bytes "TEXR"
	static constexpr size_t kMeshVertexStride = 32u;    // 3+3+2 floats

	uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags desiredFlags)
	{
		VkPhysicalDeviceMemoryProperties memProps{};
		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		{
			if ((typeBits & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & desiredFlags) == desiredFlags)
			{
				return i;
			}
		}
		return UINT32_MAX;
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

	void AssetRegistry::Init(VkDevice device, VkPhysicalDevice physicalDevice, void* vmaAllocator, const engine::core::Config& config)
	{
		std::fprintf(stderr, "[ASSET] Init enter device=%p vma=%p\n", (void*)device, vmaAllocator); std::fflush(stderr);
		m_device = device;
		m_physicalDevice = physicalDevice;
		m_vmaAllocator = vmaAllocator;
		m_config = &config;
		std::fprintf(stderr, "[ASSET] Init OK\n"); std::fflush(stderr);
		LOG_INFO(Render, "[AssetRegistry] Init OK");
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
		std::fprintf(stderr, "[ASSET] Destroy enter meshes=%zu textures=%zu\n", m_meshes.size(), m_textures.size()); std::fflush(stderr);
		if (m_device == VK_NULL_HANDLE)
		{
			std::fprintf(stderr, "[ASSET] Destroy OK\n"); std::fflush(stderr);
			LOG_INFO(Render, "[AssetRegistry] Destroyed");
			return;
		}
		for (auto& p : m_meshes)
		{
			if (p.second.vertexBuffer != VK_NULL_HANDLE && p.second.vertexAlloc)
			{
				VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(p.second.vertexAlloc);
				vkDestroyBuffer(m_device, p.second.vertexBuffer, nullptr);
				vkFreeMemory(m_device, mem, nullptr);
			}
			if (p.second.indexBuffer != VK_NULL_HANDLE && p.second.indexAlloc)
			{
				VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(p.second.indexAlloc);
				vkDestroyBuffer(m_device, p.second.indexBuffer, nullptr);
				vkFreeMemory(m_device, mem, nullptr);
			}
		}
		m_meshes.clear();
		m_meshPathToId.clear();
		for (auto& p : m_textures)
		{
			if (p.second.view != VK_NULL_HANDLE) vkDestroyImageView(m_device, p.second.view, nullptr);
			if (p.second.image != VK_NULL_HANDLE && p.second.allocation)
			{
				VkDeviceMemory mem = reinterpret_cast<VkDeviceMemory>(p.second.allocation);
				vkDestroyImage(m_device, p.second.image, nullptr);
				vkFreeMemory(m_device, mem, nullptr);
			}
		}
		m_textures.clear();
		m_texturePathToId.clear();
		m_device = VK_NULL_HANDLE;
		m_physicalDevice = VK_NULL_HANDLE;
		m_vmaAllocator = nullptr;
		m_config = nullptr;
		m_nextMeshId = 1;
		m_nextTextureId = 1;
		std::fprintf(stderr, "[ASSET] Destroy OK\n"); std::fflush(stderr);
		LOG_INFO(Render, "[AssetRegistry] Destroyed");
	}

	AssetId AssetRegistry::loadMeshInternal(std::string_view relativePath)
	{
		std::fprintf(stderr, "[ASSET] loadMesh '%.*s'\n", static_cast<int>(relativePath.size()), relativePath.data()); std::fflush(stderr);
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
		if (numVertices > 0)
		{
			const float* first = reinterpret_cast<const float*>(vertexData);
			asset.localBoundsMin = { first[0], first[1], first[2] };
			asset.localBoundsMax = asset.localBoundsMin;
			for (uint32_t vertexIndex = 1; vertexIndex < numVertices; ++vertexIndex)
			{
				const float* position = reinterpret_cast<const float*>(vertexData + static_cast<size_t>(vertexIndex) * kMeshVertexStride);
				asset.localBoundsMin.x = std::min(asset.localBoundsMin.x, position[0]);
				asset.localBoundsMin.y = std::min(asset.localBoundsMin.y, position[1]);
				asset.localBoundsMin.z = std::min(asset.localBoundsMin.z, position[2]);
				asset.localBoundsMax.x = std::max(asset.localBoundsMax.x, position[0]);
				asset.localBoundsMax.y = std::max(asset.localBoundsMax.y, position[1]);
				asset.localBoundsMax.z = std::max(asset.localBoundsMax.z, position[2]);
			}
			asset.hasLocalBounds = true;
		}

		auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
			VkBuffer& outBuffer, void*& outAlloc) -> bool
		{
			VkBufferCreateInfo bufInfo{};
			bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufInfo.size = size;
			bufInfo.usage = usage;
			bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			if (vkCreateBuffer(m_device, &bufInfo, nullptr, &outBuffer) != VK_SUCCESS || outBuffer == VK_NULL_HANDLE)
				return false;

			VkMemoryRequirements memReq{};
			vkGetBufferMemoryRequirements(m_device, outBuffer, &memReq);
			uint32_t memTypeIdx = FindMemoryType(m_physicalDevice, memReq.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
			if (memTypeIdx == UINT32_MAX)
			{
				vkDestroyBuffer(m_device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memReq.size;
			allocInfo.memoryTypeIndex = memTypeIdx;

			VkDeviceMemory mem = VK_NULL_HANDLE;
			if (vkAllocateMemory(m_device, &allocInfo, nullptr, &mem) != VK_SUCCESS || mem == VK_NULL_HANDLE)
			{
				vkDestroyBuffer(m_device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			if (vkBindBufferMemory(m_device, outBuffer, mem, 0) != VK_SUCCESS)
			{
				vkFreeMemory(m_device, mem, nullptr);
				vkDestroyBuffer(m_device, outBuffer, nullptr);
				outBuffer = VK_NULL_HANDLE;
				return false;
			}

			outAlloc = reinterpret_cast<void*>(mem);
			return true;
		};

		if (!createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, asset.vertexBuffer, asset.vertexAlloc))
			return kInvalidAssetId;
		if (!createBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, asset.indexBuffer, asset.indexAlloc))
		{
			VkDeviceMemory vMem = reinterpret_cast<VkDeviceMemory>(asset.vertexAlloc);
			vkDestroyBuffer(m_device, asset.vertexBuffer, nullptr);
			vkFreeMemory(m_device, vMem, nullptr);
			return kInvalidAssetId;
		}

		void* pv = nullptr;
		VkDeviceMemory vMem = reinterpret_cast<VkDeviceMemory>(asset.vertexAlloc);
		if (vkMapMemory(m_device, vMem, 0, vertexBytes, 0, &pv) != VK_SUCCESS)
		{
			VkDeviceMemory iMem = reinterpret_cast<VkDeviceMemory>(asset.indexAlloc);
			vkDestroyBuffer(m_device, asset.indexBuffer, nullptr);
			vkFreeMemory(m_device, iMem, nullptr);
			vkDestroyBuffer(m_device, asset.vertexBuffer, nullptr);
			vkFreeMemory(m_device, vMem, nullptr);
			return kInvalidAssetId;
		}
		memcpy(pv, vertexData, vertexBytes);
		vkUnmapMemory(m_device, vMem);

		void* pi = nullptr;
		VkDeviceMemory iMem = reinterpret_cast<VkDeviceMemory>(asset.indexAlloc);
		if (vkMapMemory(m_device, iMem, 0, indexBytes, 0, &pi) != VK_SUCCESS)
		{
			vkDestroyBuffer(m_device, asset.indexBuffer, nullptr);
			vkFreeMemory(m_device, iMem, nullptr);
			vkDestroyBuffer(m_device, asset.vertexBuffer, nullptr);
			vkFreeMemory(m_device, vMem, nullptr);
			return kInvalidAssetId;
		}
		memcpy(pi, indexData, indexBytes);
		vkUnmapMemory(m_device, iMem);

		AssetId id = m_nextMeshId++;
		m_meshes[id] = std::move(asset);
		LOG_INFO(Render, "AssetRegistry: loaded mesh {} ({} vertices, {} indices)", relativePath, numVertices, numIndices);
		LOG_INFO(Render, "[AssetRegistry] Mesh bounds ready (path={}, min=({}, {}, {}), max=({}, {}, {}))",
			relativePath,
			asset.localBoundsMin.x, asset.localBoundsMin.y, asset.localBoundsMin.z,
			asset.localBoundsMax.x, asset.localBoundsMax.y, asset.localBoundsMax.z);
		return id;
	}

	AssetId AssetRegistry::loadTextureInternal(std::string_view relativePath, bool useSrgb)
	{
		std::fprintf(stderr, "[ASSET] loadTexture '%.*s'\n", static_cast<int>(relativePath.size()), relativePath.data()); std::fflush(stderr);
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

		if (vkCreateImage(m_device, &imgInfo, nullptr, &asset.image) != VK_SUCCESS || asset.image == VK_NULL_HANDLE)
			return kInvalidAssetId;

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(m_device, asset.image, &memReq);
		uint32_t memTypeIdx = FindMemoryType(m_physicalDevice, memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (memTypeIdx == UINT32_MAX)
		{
			vkDestroyImage(m_device, asset.image, nullptr);
			return kInvalidAssetId;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIdx;
		VkDeviceMemory imgMem = VK_NULL_HANDLE;
		if (vkAllocateMemory(m_device, &allocInfo, nullptr, &imgMem) != VK_SUCCESS || imgMem == VK_NULL_HANDLE)
		{
			vkDestroyImage(m_device, asset.image, nullptr);
			return kInvalidAssetId;
		}
		if (vkBindImageMemory(m_device, asset.image, imgMem, 0) != VK_SUCCESS)
		{
			vkFreeMemory(m_device, imgMem, nullptr);
			vkDestroyImage(m_device, asset.image, nullptr);
			return kInvalidAssetId;
		}
		asset.allocation = reinterpret_cast<void*>(imgMem);

		void* ptr = nullptr;
		if (vkMapMemory(m_device, imgMem, 0, memReq.size, 0, &ptr) == VK_SUCCESS)
		{
			VkImageSubresource subres{};
			subres.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subres.mipLevel = 0;
			subres.arrayLayer = 0;
			VkSubresourceLayout layout{};
			vkGetImageSubresourceLayout(m_device, asset.image, &subres, &layout);
			const uint8_t* src = pixels;
			uint8_t* dst = static_cast<uint8_t*>(ptr);
			for (uint32_t y = 0; y < height; ++y)
				memcpy(dst + y * layout.rowPitch, src + y * width * 4, width * 4);
			vkUnmapMemory(m_device, imgMem);
		}
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
		if (vkCreateImageView(m_device, &viewInfo, nullptr, &asset.view) != VK_SUCCESS)
		{
			vkDestroyImage(m_device, asset.image, nullptr);
			vkFreeMemory(m_device, imgMem, nullptr);
			return kInvalidAssetId;
		}
		AssetId id = m_nextTextureId++;
		m_textures[id] = std::move(asset);
		LOG_INFO(Render, "AssetRegistry: loaded texture {} ({}x{}, {})", relativePath, width, height, useSrgb ? "sRGB" : "linear");
		return id;
	}
}
