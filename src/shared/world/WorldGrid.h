#pragma once

// Grille du monde partagée par le client, les serveurs et les outils :
// taille de zone, de chunk et de cellule spatiale (intérêt / réplication).
// Extrait de src/client/world/WorldModel.h pour que le serveur n'inclue plus
// de code client (valeurs inchangées).

namespace engine::world
{
	/// Zone side length in meters (streaming / zone-local space). **10 km** exact.
	constexpr int kZoneSize = 10000;
	/// Chunk side length in meters (must divide \ref kZoneSize). **500 m** → 20×20 chunks sur 10 km.
	/// (512 m ne divise pas 10 000 ; 500 m reste proche de la cible « ~512 ».)
	constexpr int kChunkSize = 500;
	/// Number of chunks per zone axis (kZoneSize / kChunkSize).
	constexpr int kChunksPerZoneAxis = kZoneSize / kChunkSize;

	/// Grille spatiale intérêt / réplication (m) côté serveur ; doit diviser \ref kZoneSize.
	/// **100 m** → 100×100 cellules ; divise aussi \ref kChunkSize (5×5 cellules par chunk).
	constexpr int kSpatialCellSizeMeters = 100;

	static_assert(kZoneSize % kChunkSize == 0, "kZoneSize must be an integer multiple of kChunkSize");
	static_assert(kZoneSize % kSpatialCellSizeMeters == 0,
	              "kZoneSize must be an integer multiple of kSpatialCellSizeMeters");
	static_assert(kChunkSize % kSpatialCellSizeMeters == 0,
	              "kChunkSize should align with spatial cells (tooling / debug consistency)");
}
