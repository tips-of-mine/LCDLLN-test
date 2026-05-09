#include "src/client/render/terrain_chunk/LayerArrayLoader.h"

namespace engine::render::terrain_chunk
{
	std::filesystem::path ResolveLayerAssetPath(
		const engine::world::terrain::LayerPalette& palette,
		uint32_t layerIndex,
		LayerMapType mapType,
		std::string_view contentRoot,
		const std::function<bool(const std::filesystem::path&)>& fileExists)
	{
		// Clamp safe : si layerIndex hors bornes, fallback sur 0 (pattern
		// SplatMap::MakeUniform).
		if (layerIndex >= palette.layers.size()) layerIndex = 0u;
		const auto& entry = palette.layers[layerIndex];

		// Sélectionne le path relatif selon le mapType.
		std::string relPath;
		switch (mapType)
		{
		case LayerMapType::Albedo: relPath = entry.albedoPath; break;
		case LayerMapType::Normal: relPath = entry.normalPath; break;
		case LayerMapType::Arm:    relPath = entry.armPath;    break;
		}

		// 1ère tentative : chemin déclaré (ex. tex/terrain/dirt_albedo.texr).
		const std::filesystem::path texr = std::filesystem::path(contentRoot) / relPath;
		if (fileExists(texr)) return texr;

		// Fallback : terrain/placeholders/<name>.png (livré en Phase 3a).
		return std::filesystem::path(contentRoot)
			/ "terrain"
			/ "placeholders"
			/ (entry.name + ".png");
	}

	bool LayerArrayLoader::Init(IGpuImageArrayAllocator* alloc,
		const engine::world::terrain::LayerPalette& palette,
		std::string_view contentRoot, std::string& outError)
	{
		// L'impl complète (chargement PNG via stb_image + concat 8 layers en
		// blob + CreateAndUploadRGBA8Array + 2 samplers) vit dans Task 11
		// (`TerrainChunkRenderer`) où on a accès au vrai allocator Vulkan.
		//
		// Cette task livre :
		//   - Le squelette `Init/Shutdown` (membres + retain alloc).
		//   - La résolution de path testée (`ResolveLayerAssetPath`).
		//
		// Quand l'impl complète arrive (Task 11), elle utilisera
		// `ResolveLayerAssetPath` pour les 24 chemins puis chargera via
		// stb_image, concatena les 8 layers d'un mapType en un seul blob,
		// puis appelera `CreateAndUploadRGBA8Array(width, height, 8, blob,
		// outImage, outView)` pour chacun des 3 mapTypes.
		m_alloc = alloc;
		(void)palette;
		(void)contentRoot;
		(void)outError;
		// Init renvoie true même sans charger (les ressources restent à
		// VK_NULL_HANDLE) : permet à `TerrainChunkRenderer` d'instancier
		// le loader en boot et de filler les ressources dans une étape
		// ultérieure (cf. TerrainChunkRenderer Task 11).
		return true;
	}

	void LayerArrayLoader::Shutdown()
	{
		if (m_alloc == nullptr) return;
		if (m_res.albedoArrayImage) m_alloc->DestroyImage(m_res.albedoArrayImage, m_res.albedoArrayView);
		if (m_res.normalArrayImage) m_alloc->DestroyImage(m_res.normalArrayImage, m_res.normalArrayView);
		if (m_res.armArrayImage)    m_alloc->DestroyImage(m_res.armArrayImage,    m_res.armArrayView);
		if (m_res.nearestSampler)   m_alloc->DestroySampler(m_res.nearestSampler);
		if (m_res.linearSampler)    m_alloc->DestroySampler(m_res.linearSampler);
		m_res = LayerArrayResources{};
		m_alloc = nullptr;
	}
}
