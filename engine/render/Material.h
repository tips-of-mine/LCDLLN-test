#pragma once

#include "engine/render/AssetRegistry.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>

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

	/// Manages Vulkan descriptor set layout, pool, samplers, and fallback 1x1 textures
	/// for per-material descriptor sets.
	///
	/// Descriptor set layout (set = 0):
	///   binding 0 : combined image sampler — BaseColor (sRGB-compatible sampler)
	///   binding 1 : combined image sampler — Normal    (linear sampler)
	///   binding 2 : combined image sampler — ORM       (linear sampler)
	///
	/// Usage:
	///   1. Init(device, physicalDevice)
	///   2. Pass GetLayout() to GeometryPass::Init as the material descriptor set layout.
	///   3. For each material: set = CreateDescriptorSet(device, mat)
	///   4. Pass set to GeometryPass::Record each frame.
	///   5. Destroy(device) on shutdown.
	class MaterialDescriptorCache
	{
	public:
		MaterialDescriptorCache() = default;
		MaterialDescriptorCache(const MaterialDescriptorCache&) = delete;
		MaterialDescriptorCache& operator=(const MaterialDescriptorCache&) = delete;

		/// Creates samplers, fallback 1×1 textures, descriptor set layout, and pool.
		/// \param maxMaterials  Maximum number of descriptor sets that can be allocated.
		bool Init(VkDevice device, VkPhysicalDevice physicalDevice,
		          uint32_t maxMaterials = 64);

		/// Allocates and writes one descriptor set for the given material.
		/// Uses internal fallback textures for any invalid handles in mat.
		/// Returns VK_NULL_HANDLE on failure.
		VkDescriptorSet CreateDescriptorSet(VkDevice device, const Material& mat);

		/// Returns the descriptor set layout for use in pipeline layout creation.
		VkDescriptorSetLayout GetLayout() const { return m_layout; }

		/// Releases all owned Vulkan resources. Safe to call multiple times.
		void Destroy(VkDevice device);

		/// Returns true if Init succeeded and Destroy has not been called.
		bool IsValid() const { return m_layout != VK_NULL_HANDLE; }

	private:
		VkDescriptorSetLayout m_layout        = VK_NULL_HANDLE;
		VkDescriptorPool      m_pool          = VK_NULL_HANDLE;
		VkSampler             m_samplerSrgb   = VK_NULL_HANDLE; ///< Linear filter, repeat — BaseColor.
		VkSampler             m_samplerLinear = VK_NULL_HANDLE; ///< Linear filter, repeat — Normal/ORM.

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
	};

} // namespace engine::render
