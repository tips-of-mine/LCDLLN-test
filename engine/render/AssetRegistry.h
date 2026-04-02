#pragma once

#include "engine/core/Config.h"
#include "engine/math/Math.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Stable asset identifier (0 = invalid). Used for cache lookup and handle validity.
	using AssetId = uint32_t;
	constexpr AssetId kInvalidAssetId = 0;

	/// Maximum LOD levels in the chain (M09.3).
	constexpr uint32_t kMeshLodLevelCount = 4;

	/// GPU mesh data: vertex and index buffers (owned by registry). Allocations store
	/// VkDeviceMemory as opaque void* for compatibility with older VMA-based code.
	/// M09.3: optional LOD chain — when lodLevelCount > 0, each LOD has indexOffset/indexCount in the same index buffer.
	struct MeshAsset
	{
		VkBuffer vertexBuffer = VK_NULL_HANDLE;
		VkBuffer indexBuffer = VK_NULL_HANDLE;
		void* vertexAlloc = nullptr; ///< VkDeviceMemory
		void* indexAlloc = nullptr; ///< VkDeviceMemory
		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;

		/// M09.3: LOD chain. 0 = single LOD (use indexCount for LOD0). 1..kMeshLodLevelCount = number of LODs.
		uint32_t lodLevelCount = 0;
		uint32_t lodIndexOffset[kMeshLodLevelCount] = { 0, 0, 0, 0 };
		uint32_t lodIndexCount[kMeshLodLevelCount] = { 0, 0, 0, 0 };
		engine::math::Vec3 localBoundsMin{ 0.0f, 0.0f, 0.0f };
		engine::math::Vec3 localBoundsMax{ 0.0f, 0.0f, 0.0f };
		bool hasLocalBounds = false;

		/// Returns index count for the given LOD level (0..3). When lodLevelCount==0, returns indexCount for LOD0 only.
		uint32_t GetLodIndexCount(uint32_t lodLevel) const
		{
			if (lodLevelCount == 0)
				return (lodLevel == 0) ? indexCount : 0;
			if (lodLevel >= kMeshLodLevelCount) return 0;
			return lodIndexCount[lodLevel];
		}
		/// Returns index buffer offset (in indices) for the given LOD level. When lodLevelCount==0, returns 0 for LOD0.
		uint32_t GetLodIndexOffset(uint32_t lodLevel) const
		{
			if (lodLevelCount == 0)
				return (lodLevel == 0) ? 0u : 0u;
			if (lodLevel >= kMeshLodLevelCount) return 0;
			return lodIndexOffset[lodLevel];
		}
	};

	/// GPU texture data: image and view (owned by registry). Allocation stores
	/// VkDeviceMemory as opaque void* for compatibility with older VMA-based code.
	struct TextureAsset
	{
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		void* allocation = nullptr; ///< VkDeviceMemory
		uint32_t width = 0;
		uint32_t height = 0;
	};

	/// Stable handle to a mesh asset. Invalid after registry destroy or if load failed.
	class MeshHandle
	{
	public:
		MeshHandle() = default;
		MeshHandle(class AssetRegistry* registry, AssetId id) : m_registry(registry), m_id(id) {}

		/// Returns true if the handle refers to a loaded mesh.
		bool IsValid() const;
		/// Returns the mesh asset, or nullptr if invalid.
		MeshAsset* Get() const;
		AssetId Id() const { return m_id; }

	private:
		class AssetRegistry* m_registry = nullptr;
		AssetId m_id = kInvalidAssetId;
	};

	/// Stable handle to a texture asset. Invalid after registry destroy or if load failed.
	class TextureHandle
	{
	public:
		TextureHandle() = default;
		TextureHandle(class AssetRegistry* registry, AssetId id) : m_registry(registry), m_id(id) {}

		/// Returns true if the handle refers to a loaded texture.
		bool IsValid() const;
		/// Returns the texture asset, or nullptr if invalid.
		TextureAsset* Get() const;
		AssetId Id() const { return m_id; }

	private:
		class AssetRegistry* m_registry = nullptr;
		AssetId m_id = kInvalidAssetId;
	};

	/// Asset registry: cache by path, load mesh/texture from content path, ownership explicit (destroy on shutdown).
	/// Paths are relative to config paths.content (e.g. "meshes/test.mesh", "textures/test.texr").
	class AssetRegistry
	{
	public:
		AssetRegistry() = default;
		AssetRegistry(const AssetRegistry&) = delete;
		AssetRegistry& operator=(const AssetRegistry&) = delete;

		/// Initializes the registry for loading. vmaAllocator = centralised GPU allocator (VMA); cast to VmaAllocator in impl.
		void Init(VkDevice device, VkPhysicalDevice physicalDevice, void* vmaAllocator, const engine::core::Config& config);

		/// Loads a mesh from content path (relative to paths.content). Returns cached asset if already loaded.
		/// Format: minimal binary .mesh (see implementation). Returns invalid handle on failure.
		MeshHandle LoadMesh(std::string_view relativePath);

		/// Loads a texture from content path. Returns cached asset if already loaded.
		/// \param useSrgb If true, use sRGB format; otherwise linear.
		/// Format: raw .texr (magic, width, height, sRGB flag, RGBA pixels) ou PNG (8-bit RGBA).
		TextureHandle LoadTexture(std::string_view relativePath, bool useSrgb = false);

		/// Charge une PNG pour blit plein écran vers la swapchain (canal BGRA si la surface est B8G8R8A8).
		/// Ignoré pour les .texr (même chargement que \ref LoadTexture).
		TextureHandle LoadTextureForPresentBlit(std::string_view relativePath, VkFormat swapchainColorFormat);

		/// Copie la PNG « pending » vers une image GPU OPTIMAL (requis pour \c vkCmdBlitImage sur la plupart des GPU).
		/// À appeler juste après \ref LoadTextureForPresentBlit si le handle est valide.
		bool FinalizePresentBlitTextureUpload(VkQueue graphicsQueue, uint32_t graphicsQueueFamilyIndex);

		/// Returns the mesh for the given id, or nullptr.
		MeshAsset* GetMesh(AssetId id) const;
		/// Returns the texture for the given id, or nullptr.
		TextureAsset* GetTexture(AssetId id) const;

		/// Releases all GPU resources and clears cache. Call on shutdown.
		void Destroy();

		/// Returns true if Init was called and Destroy has not been called.
		bool IsValid() const { return m_device != VK_NULL_HANDLE; }

	private:
		VkDevice m_device = VK_NULL_HANDLE;
		VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
		void* m_vmaAllocator = nullptr;
		const engine::core::Config* m_config = nullptr;

		AssetId m_nextMeshId = 1;
		AssetId m_nextTextureId = 1;
		std::unordered_map<std::string, AssetId> m_meshPathToId;
		std::unordered_map<std::string, AssetId> m_texturePathToId;
		std::unordered_map<AssetId, MeshAsset> m_meshes;
		std::unordered_map<AssetId, TextureAsset> m_textures;

		AssetId loadMeshInternal(std::string_view relativePath);
		AssetId loadTextureInternal(std::string_view relativePath, bool useSrgb, VkFormat presentBlitDstFormat = VK_FORMAT_UNDEFINED);

		void ReleasePendingPresentBlitStaging();

		struct PendingPresentBlitUpload
		{
			AssetId textureId = kInvalidAssetId;
			VkBuffer stagingBuffer = VK_NULL_HANDLE;
			VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
			uint32_t width = 0;
			uint32_t height = 0;
			VkFormat format = VK_FORMAT_UNDEFINED;
		};
		PendingPresentBlitUpload m_pendingPresentBlit{};
	};
}
