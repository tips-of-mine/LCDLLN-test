#pragma once

#include "engine/render/AssetRegistry.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::render
{
	/// Flags for material properties (reserved for future use).
	enum class MaterialFlags : uint32_t
	{
		None = 0,
	};

	/// Per-material texture handles + tiling parameters.
	///
	/// Texture space conventions (M03.3):
	///   - baseColor : sRGB   (hardware linearises on GPU sample via SRGB format)
	///   - normal    : linear (tangent-space, R=X G=Y B=Z encoded [0,1])
	///   - orm       : linear (R=AO, G=Roughness, B=Metallic — UE4 ORM packing)
	///
	/// Any invalid handle falls back to the 1x1 default textures managed by
	/// MaterialDescriptorCache::Init().
	struct Material
	{
		TextureHandle baseColor; ///< sRGB BaseColor / Albedo texture.
		TextureHandle normal;    ///< Linear tangent-space Normal map.
		TextureHandle orm;       ///< Linear ORM texture (R=AO, G=Roughness, B=Metallic).
		float         tiling[2] = { 1.0f, 1.0f }; ///< UV tiling scale (s, t).
		MaterialFlags flags     = MaterialFlags::None;
	};

	/// GPU-side material payload used by the bindless geometry shader path.
	struct MaterialGpuData
	{
		uint32_t baseColorIndex = 0;
		uint32_t normalIndex = 1;
		uint32_t ormIndex = 2;
		uint32_t flags = 0;
		float tiling[2] = { 1.0f, 1.0f };
		float padding[2] = { 0.0f, 0.0f };
	};

	/// Manages a global bindless texture array and a material buffer storing texture indices.
	///
	/// Descriptor set layout (set = 0):
	///   binding 0 : combined image sampler array — global textures[]
	///   binding 1 : storage buffer — MaterialGpuData materials[]
	///
	/// Usage:
	///   1. Init(device, physicalDevice)
	///   2. Pass GetLayout() to GeometryPass::Init as the material descriptor set layout.
	///   3. Bind GetDescriptorSet() once per draw path.
	///   4. Pass CreateMaterial(device, mat) result as material index.
	///   5. Destroy(device) on shutdown.
	class MaterialDescriptorCache
	{
	public:
		MaterialDescriptorCache() = default;
		MaterialDescriptorCache(const MaterialDescriptorCache&) = delete;
		MaterialDescriptorCache& operator=(const MaterialDescriptorCache&) = delete;

		/// Creates samplers, fallback textures, the global descriptor set, and the material buffer.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
		          uint32_t maxTextures = 64,
		          uint32_t maxMaterials = 64);

		/// Allocates a material slot, resolves its texture slots in the global array, and writes it into the GPU material buffer.
		/// Returns material index 0 on failure or when the default fallback material is requested.
		uint32_t CreateMaterial(VkDevice device, const Material& mat);

		/// Returns the descriptor set layout for use in pipeline layout creation.
		VkDescriptorSetLayout GetLayout() const { return m_layout; }
		/// Returns the global descriptor set to bind before geometry draws.
		VkDescriptorSet GetDescriptorSet() const { return m_descriptorSet; }
		/// Returns the default material index (fallback textures only).
		uint32_t GetDefaultMaterialIndex() const { return 0u; }

		/// Releases all owned Vulkan resources. Safe to call multiple times.
		void Destroy(VkDevice device);

		/// Returns true if Init succeeded and Destroy has not been called.
		bool IsValid() const { return m_layout != VK_NULL_HANDLE; }

	private:
		VkDescriptorSetLayout m_layout        = VK_NULL_HANDLE;
		VkDescriptorPool      m_pool          = VK_NULL_HANDLE;
		VkDescriptorSet       m_descriptorSet = VK_NULL_HANDLE;
		VkSampler             m_samplerSrgb   = VK_NULL_HANDLE; ///< Linear filter, repeat — BaseColor.
		VkSampler             m_samplerLinear = VK_NULL_HANDLE; ///< Linear filter, repeat — Normal/ORM.
		VkBuffer              m_materialBuffer = VK_NULL_HANDLE;
		VkDeviceMemory        m_materialMemory = VK_NULL_HANDLE;
		uint32_t              m_maxTextures = 0;
		uint32_t              m_maxMaterials = 0;

		/// Fallback 1×1 GPU textures (owned by cache, not by AssetRegistry):
		///   [0] = white   R8G8B8A8_SRGB  (default BaseColor)
		///   [1] = flat    R8G8B8A8_UNORM (default Normal:  128,128,255,255)
		///   [2] = default R8G8B8A8_UNORM (default ORM:     AO=1, Rough=0.5, Metal=0)
		VkImage        m_fallbackImage[3]  = {};
		VkImageView    m_fallbackView[3]   = {};
		VkDeviceMemory m_fallbackMemory[3] = {};

		/// Creates a single 1×1 fallback texture with the given RGBA data and format.
		bool createFallbackTexture(VkDevice device, VkPhysicalDevice physDev,
		                           uint32_t idx, const uint8_t rgba[4],
		                           VkFormat format);
		bool createMaterialBuffer(VkDevice device, VkPhysicalDevice physicalDevice);
		bool writeTextureDescriptors(VkDevice device);
		uint32_t acquireTextureSlot(const TextureHandle& texture, VkSampler sampler, VkImageView fallbackView);
		bool writeMaterialData(VkDevice device, uint32_t materialIndex, const MaterialGpuData& gpuData);

		struct TextureSlot
		{
			AssetId assetId = kInvalidAssetId;
			VkImageView view = VK_NULL_HANDLE;
			VkSampler sampler = VK_NULL_HANDLE;
		};

		std::vector<TextureSlot> m_textureSlots;
		std::vector<MaterialGpuData> m_materials;
		std::unordered_map<uint64_t, uint32_t> m_textureKeyToSlot;
	};

} // namespace engine::render
