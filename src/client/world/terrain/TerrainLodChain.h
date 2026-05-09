#pragma once

#include "src/client/world/terrain/TerrainChunk.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::world::terrain
{
	/// Magic du fichier `chunks/chunk_i_j/terrain_lods.bin` ("TRLO" little-endian).
	constexpr uint32_t kTerrainLodsMagic = 0x4F4C5254u;
	/// Version courante du payload `terrain_lods.bin` (M100.8).
	constexpr uint32_t kTerrainLodsVersion = 1u;
	/// Nombre de niveaux LOD persistés (LOD1..LOD3 ; LOD0 vit dans `terrain.bin`).
	constexpr uint32_t kPersistedLodCount = 3u;
	/// Résolutions par niveau, alignées sur la division par 2 de (257 - 1).
	constexpr std::array<uint32_t, 3> kLodResolutions{129u, 65u, 33u};
	/// Cell size par niveau (en mètres). Multiplié par 2 à chaque LOD.
	constexpr std::array<float, 3> kLodCellSizes{2.0f, 4.0f, 8.0f};

	/// Un niveau de LOD réduit, généré par box filter 2×2 sur le LOD parent.
	/// Pas de skirt — la skirt géométrique est ajoutée à la construction du
	/// mesh GPU par `BuildLodMesh(withSkirt=true)`.
	struct TerrainLod
	{
		uint32_t resolution = 0;
		float cellSizeMeters = 0.0f;
		std::vector<float> heights;
	};

	/// Chaîne complète des 3 LODs persistés (ordre LOD1, LOD2, LOD3).
	struct TerrainLodChain
	{
		std::array<TerrainLod, 3> lods;
	};

	/// Génère LOD1..LOD3 à partir de `lod0` (le `TerrainChunk` LOD0). Box
	/// filter 2×2 par niveau. Déterministe pour mêmes inputs.
	TerrainLodChain GenerateLodChain(const TerrainChunk& lod0);

	/// Sérialise la chaîne au format `terrain_lods.bin`. Header
	/// `OutputVersionHeader` (magic=`kTerrainLodsMagic`, contentHash=xxhash64
	/// du payload post-header), puis : uint32 lodCount + pour chaque LOD :
	/// uint32 resolution + float cellSizeMeters + float[resolution²] heights.
	bool SaveTerrainLodsBin(const TerrainLodChain& chain,
		std::vector<uint8_t>& outBytes, std::string& outError);

	/// Désérialise un `terrain_lods.bin` complet. Valide magic, version,
	/// contentHash, lodCount, et que chaque (resolution, cellSize) match
	/// `kLodResolutions`/`kLodCellSizes`.
	bool LoadTerrainLodsBin(std::span<const uint8_t> bytes,
		TerrainLodChain& outChain, std::string& outError);
}
