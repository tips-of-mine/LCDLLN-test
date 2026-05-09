#pragma once

#include "engine/world/OutputVersion.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::terrain
{
	/// Magic du fichier `chunks/chunk_i_j/splat.bin` ("SLAT" little-endian).
	/// Distinct de `kTerrainSplatFileMagic` (= "SLAP", `0x50414C53u`) du legacy
	/// `engine::render::terrain::TerrainSplatting` qui sert le mode démo
	/// single-zone non-chunked. Coexistence intentionnelle (pas de migration
	/// dans cette PR — ticket ciblé futur).
	constexpr uint32_t kSplatMagic = 0x54414C53u;

	/// Version courante du payload `splat.bin` (M100.9).
	constexpr uint32_t kSplatVersion = 1u;

	/// Résolution par chunk (alignée sur `TerrainChunk` 257²).
	constexpr uint32_t kSplatResolution = 257u;

	/// Nombre de layers par cellule (8 octets de poids).
	constexpr uint32_t kSplatLayerCount = 8u;

	/// Splat-map éditable d'un chunk monde (M100.9). Stockage row-major Z-major
	/// `weights[(z * resolution + x) * layerCount + layer]`. Invariant : pour
	/// chaque cellule (x, z), la somme des `layerCount` poids vaut exactement
	/// `255` (validé au `Load` et après chaque commit `SplatPaintCommand`).
	struct SplatMap
	{
		uint32_t resolution = kSplatResolution;
		uint32_t layerCount = kSplatLayerCount;
		std::vector<uint8_t> weights; // taille = resolution * resolution * layerCount

		/// Initialise une splat-map où toutes les cellules ont `layerIndex=255`
		/// et les autres layers à 0. Somme=255 invariant satisfait par
		/// construction. Si `layerIndex >= kSplatLayerCount`, fallback sur
		/// `0` (= dirt par convention `layer_palette.json`).
		static SplatMap MakeUniform(uint32_t layerIndex);

		/// Vérifie l'invariant somme=255 par cellule + dimensions cohérentes.
		/// Coût O(resolution²).
		/// \return true si toutes les cellules respectent l'invariant.
		bool IsValid() const;
	};

	/// Sérialise `splat` au format `splat.bin` (M100.9). Header
	/// `OutputVersionHeader` (magic=`kSplatMagic`, version=`kSplatVersion`,
	/// `contentHash` = xxhash64 du payload post-header), puis :
	/// uint32 resolution + uint32 layerCount + uint8[res²*layers] weights.
	/// \return true si écriture OK et `IsValid()` est vrai. Sinon `outError`
	/// renseigné et `outBytes` non garanti.
	bool SaveSplatBin(const SplatMap& splat, std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise un `splat.bin`. Valide magic, version, contentHash,
	/// resolution (== `kSplatResolution`), layerCount (== `kSplatLayerCount`),
	/// puis l'invariant somme=255 par cellule.
	bool LoadSplatBin(std::span<const uint8_t> bytes, SplatMap& outSplat, std::string& outError);
}
