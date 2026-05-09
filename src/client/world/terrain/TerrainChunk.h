#pragma once

#include "src/client/world/OutputVersion.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::terrain
{
	/// Résolution fixe d'un chunk de heightmap (256 quads + 1 vertex de bord = 257²).
	/// Choisie pour permettre 4 niveaux de LOD par division par 2 sans perte de bord
	/// (257 → 129 → 65 → 33). M100.8 stocke LOD1..LOD3, LOD0 = ce TerrainChunk.
	constexpr uint32_t kTerrainResolution = 257;

	/// Taille d'une cellule en mètres monde (1 m fixe en M100). Le chunk total
	/// fait donc 256 m × 256 m, alignement avec `engine::world::Chunk`.
	constexpr float kTerrainCellSizeMeters = 1.0f;

	/// Bornes de hauteur valides (mètres). Tout `terrain.bin` chargé hors de ces
	/// bornes est rejeté avec un message d'erreur.
	constexpr float kTerrainHeightMinMeters = -1024.0f;
	constexpr float kTerrainHeightMaxMeters = 8192.0f;

	/// Magic du fichier `chunks/chunk_i_j/terrain.bin` ("TRRN" little-endian).
	constexpr uint32_t kTerrainMagic = 0x4E525254u;
	/// Version courante du payload `terrain.bin` (M100.5).
	constexpr uint32_t kTerrainVersion = 1u;

	/// Heightmap éditable LOD0 d'un chunk monde (M100.5). Layout row-major en Z
	/// (`heights[z * resolutionX + x]`). Toutes les hauteurs sont en mètres
	/// absolus monde. La structure ne possède aucune ressource GPU ; le mesh est
	/// généré à la demande par `TerrainMeshBuilder`.
	struct TerrainChunk
	{
		uint32_t resolutionX = kTerrainResolution;
		uint32_t resolutionZ = kTerrainResolution;
		float    cellSizeMeters = kTerrainCellSizeMeters;
		float    heightMin = 0.0f;
		float    heightMax = 0.0f;
		std::vector<float> heights; // taille = resolutionX * resolutionZ

		/// Initialise un chunk plat à `height` mètres (toutes les cellules à la
		/// même valeur). `heightMin == heightMax == height` après l'appel.
		static TerrainChunk MakeFlat(float height = 0.0f);

		/// Échantillonne la hauteur en coordonnées chunk-locales (mètres).
		/// Bilinéaire à l'intérieur, clamp aux bornes hors du chunk.
		/// \param localX 0..(resolutionX-1)*cellSizeMeters
		/// \param localZ idem en Z
		float SampleHeight(float localX, float localZ) const;

		/// Recalcule `heightMin`/`heightMax` à partir du contenu de `heights`.
		/// À appeler après toute édition externe du buffer (`SaveTerrainBin`
		/// le fait avant écriture).
		void RecomputeBounds();
	};

	/// Sérialise `chunk` au format binaire `terrain.bin` (M100.5). Header
	/// `OutputVersionHeader` (magic=`kTerrainMagic`, version=`kTerrainVersion`,
	/// `contentHash` = xxhash64 du payload post-header), puis le payload.
	/// \return true si écriture OK (taille = 48 + 257*257*4 = 264 244 octets).
	/// Effet de bord : remplit `outBytes` (resize + écriture).
	bool SaveTerrainBin(const TerrainChunk& chunk, std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise un `terrain.bin` complet. Valide le header (magic, version,
	/// contentHash), les dimensions (== `kTerrainResolution`), `cellSizeMeters`
	/// (== `kTerrainCellSizeMeters`), et l'intervalle `heightMin <= heightMax`
	/// dans `[kTerrainHeightMinMeters, kTerrainHeightMaxMeters]`.
	/// \return true si OK ; en cas d'erreur, `outError` est renseigné.
	bool LoadTerrainBin(std::span<const uint8_t> bytes, TerrainChunk& outChunk, std::string& outError);
}
