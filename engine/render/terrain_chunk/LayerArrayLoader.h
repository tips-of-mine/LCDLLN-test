#pragma once

// engine/render/terrain_chunk/LayerArrayLoader.h
//
// Charge les 24 textures PBR (8 layers × 3 maps : albedo / normal / arm)
// dans 3 `VkImage2DArray` partagées + 5 samplers (1 nearest pour les
// splat-maps, 4 linear pour les arrays — un par usage typique). Boot-time,
// 1× au lifecycle.
//
// Stratégie de path resolution : pour chaque (layer, mapType), essaie
// `<contentRoot>/<palette.layers[i].albedoPath>` (ou normalPath / armPath
// selon mapType). Si le `.texr` est absent, fallback vers
// `<contentRoot>/terrain/placeholders/<layerName>.png` (PNG 4×4 colorés
// livrés en Phase 3a).
//
// La fonction de résolution `ResolveLayerAssetPath` est extraite et pure
// pour permettre les tests CPU sans Vulkan.

#include "engine/world/terrain/LayerPalette.h"

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vulkan/vulkan_core.h>

namespace engine::render::terrain_chunk
{
	enum class LayerMapType : uint8_t { Albedo, Normal, Arm };

	/// Résout le chemin disque pour `(layerIndex, mapType)`. Stratégie :
	/// d'abord essaie le path déclaré dans `palette.layers[layerIndex]`
	/// (relativement à `contentRoot`). Si `fileExists` retourne false,
	/// fallback `<contentRoot>/terrain/placeholders/<layerName>.png`.
	/// `fileExists` est injectable pour les tests (mock du fs).
	std::filesystem::path ResolveLayerAssetPath(
		const engine::world::terrain::LayerPalette& palette,
		uint32_t layerIndex,
		LayerMapType mapType,
		std::string_view contentRoot,
		const std::function<bool(const std::filesystem::path&)>& fileExists);

	/// Ressources GPU partagées par tous les chunks pour le pipeline
	/// terrain_chunk. Possédées par `LayerArrayLoader`. Bind sur le
	/// descriptor set 2 du shader.
	struct LayerArrayResources
	{
		VkImage      albedoArrayImage = VK_NULL_HANDLE; ///< 8 layers PBR albedo
		VkImage      normalArrayImage = VK_NULL_HANDLE;
		VkImage      armArrayImage    = VK_NULL_HANDLE;
		VkImageView  albedoArrayView  = VK_NULL_HANDLE;
		VkImageView  normalArrayView  = VK_NULL_HANDLE;
		VkImageView  armArrayView     = VK_NULL_HANDLE;
		VkSampler    nearestSampler   = VK_NULL_HANDLE; ///< pour splatMap0 / splatMap1
		VkSampler    linearSampler    = VK_NULL_HANDLE; ///< pour les 3 arrays
	};

	/// Interface allocateur GPU pour les VkImage 2D Array + samplers.
	/// Mockable pour les tests qui ne valident que la résolution de path.
	/// L'impl runtime concrète vit dans `TerrainChunkRenderer`.
	class IGpuImageArrayAllocator
	{
	public:
		virtual ~IGpuImageArrayAllocator() = default;

		/// Crée un VkImage 2D Array (`layerCount` layers, format RGBA8) +
		/// view + uploads les `pixelData` (concatenation de `layerCount`
		/// blobs de `width * height * 4` octets).
		virtual void CreateAndUploadRGBA8Array(uint32_t width, uint32_t height,
			uint32_t layerCount, const void* pixelData,
			VkImage& outImage, VkImageView& outView) = 0;

		/// Crée un VkSampler. `linear=true` → magFilter/minFilter LINEAR ;
		/// `linear=false` → NEAREST. AddressMode REPEAT pour le tiling.
		virtual VkSampler CreateSampler(bool linear) = 0;

		/// Libère un couple (image, view).
		virtual void DestroyImage(VkImage image, VkImageView view) = 0;

		/// Libère un sampler.
		virtual void DestroySampler(VkSampler sampler) = 0;
	};

	/// Loader des arrays PBR + samplers. Vit pendant tout le lifecycle de
	/// `TerrainChunkRenderer`.
	class LayerArrayLoader
	{
	public:
		/// Init : charge les 24 textures PBR (24 = 8 layers × 3 maps) en
		/// 3 VkImage 2D Array, crée les 2 samplers (nearest + linear).
		///
		/// Implémentation différée à Task 11 (orchestrateur) qui a accès au
		/// loader PNG / texr concret. Cette task ne livre que la résolution
		/// de path pure CPU + le squelette `Init/Shutdown`.
		bool Init(IGpuImageArrayAllocator* alloc,
			const engine::world::terrain::LayerPalette& palette,
			std::string_view contentRoot, std::string& outError);

		/// Libère toutes les ressources via l'allocator.
		void Shutdown();

		const LayerArrayResources& GetResources() const { return m_res; }

	private:
		IGpuImageArrayAllocator* m_alloc = nullptr;
		LayerArrayResources m_res;
	};
}
