#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "engine/world/surface/SurfaceType.h"

namespace engine::world::terrain
{
	/// Une entrée de la `LayerPalette` (M100.9). 8 layers, indexées 0..7.
	/// Pointes vers les chemins relatifs `paths.content` des textures PBR
	/// (albedo / normal / arm). Le champ `surfaceType` est consommé par
	/// M100.11 (`SurfaceQuery`) pour le hook gameplay.
	struct LayerEntry
	{
		uint32_t index = 0;
		std::string name;
		std::string albedoPath;
		std::string normalPath;
		std::string armPath;
		float tilingMeters = 4.0f;
		std::string surfaceTypeName;                                  // string brute du JSON, conservée pour debug
		engine::world::surface::SurfaceType surfaceType =
		    engine::world::surface::SurfaceType::Dirt;                // enum canonique parsée
	};

	/// Palette des 8 layers terrain. Lue depuis
	/// `assets/terrain/layer_palette.json` au démarrage du runtime + de
	/// l'éditeur. M100.9 fixe le nombre à 8 (`kSplatLayerCount`).
	struct LayerPalette
	{
		uint32_t version = 1u;
		std::array<LayerEntry, 8> layers;

		/// Précondition : layer < 8. Retourne l'enum canonique de la layer.
		/// (Layer index hors range : comportement non spécifié — debug-assert.)
		engine::world::surface::SurfaceType GetSurfaceTypeForLayer(uint8_t layer) const noexcept;
	};

	/// Charge `path` (JSON) dans `outPalette`. Format :
	/// `{"version":1, "layers":[{"index":0, "name":"dirt", "albedo":"...",
	/// "normal":"...", "arm":"...", "tilingMeters":4.0, "surfaceType":"Dirt"},
	/// ...8 entrées]}`.
	/// Parser linéaire minimal : tolère espaces / sauts de ligne, attend
	/// strictement 8 objets dans le tableau `layers`.
	/// \return true si OK, sinon `outError` renseigné.
	bool LoadLayerPalette(const std::filesystem::path& path,
		LayerPalette& outPalette, std::string& outError);
}
