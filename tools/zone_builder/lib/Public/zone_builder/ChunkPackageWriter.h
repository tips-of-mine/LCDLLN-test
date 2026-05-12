#pragma once

#include <zone_builder/LayoutImporter.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::world::terrain { struct TerrainChunk; struct TerrainLodChain; struct SplatMap; }
namespace engine::world::water { struct WaterScene; }
namespace engine::world::hazard { struct HazardScene; }

namespace tools::zone_builder
{
	/// Writes chunk package (M10.5): chunk.meta + geo.pak, tex.pak, instances.bin, navmesh.bin, probes.bin.
	/// \param outputDir Directory for the chunk (e.g. "build/zone_0/chunks/chunk_0_0").
	/// \param chunkX Chunk X coordinate.
	/// \param chunkZ Chunk Z coordinate.
	/// \return true on success.
	bool WriteChunkPackage(const std::string& outputDir, int32_t chunkX, int32_t chunkZ);

	/// Builds one zone output tree from a layout by chunking instances with floor(x/kChunkSize), floor(z/kChunkSize).
	/// Writes `zone.meta`, `probes.bin`, `atmosphere.json`, then `chunks/chunk_i_j/chunk.meta` and `instances.bin` under `outputRootDir`.
	/// `assetId` values written to `instances.bin` are deterministic hashes of the relative glTF path.
	/// Output headers store builder/runtime compatibility plus a shared xxHash64 content fingerprint.
	/// \param outputRootDir Root directory for the zone output (e.g. "build/zone_0").
	/// \param config Runtime config used to resolve `paths.content` while computing the content hash.
	/// \param layout Loaded layout document to split into chunks.
	/// \param outError Receives a human-readable error on failure.
	/// \return true on success.
	bool WriteChunkedZoneOutputs(std::string_view outputRootDir, const engine::core::Config& config, const LayoutDocument& layout, std::string& outError);

	/// Écrit `terrain.bin` (M100.5) dans le dossier du chunk
	/// `<outputRootDir>/chunks/chunk_<i>_<j>/`. Crée le dossier au besoin.
	/// Le contenu est sérialisé via `engine::world::terrain::SaveTerrainBin`
	/// (header `TRRN` + xxhash64). Format identique à celui que l'éditeur
	/// produit en runtime (parité éditeur ↔ client).
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteTerrainChunk(std::string_view outputRootDir, int32_t chunkX, int32_t chunkZ,
		const engine::world::terrain::TerrainChunk& chunk, std::string& outError);

	/// Écrit `terrain_lods.bin` (M100.8) dans `<outputRootDir>/chunks/chunk_<i>_<j>/`.
	/// Crée le dossier au besoin. Sérialise via
	/// `engine::world::terrain::SaveTerrainLodsBin` (header `TRLO` +
	/// xxhash64). Format identique à celui que le `TerrainLodWorker` produit
	/// en runtime (parité éditeur ↔ client).
	bool WriteTerrainLods(std::string_view outputRootDir, int32_t chunkX, int32_t chunkZ,
		const engine::world::terrain::TerrainLodChain& chain, std::string& outError);

	/// Écrit `splat.bin` (M100.9) dans `<outputRootDir>/chunks/chunk_<i>_<j>/`.
	/// Crée le dossier au besoin. Sérialise via
	/// `engine::world::terrain::SaveSplatBin` (header `SLAT` + xxhash64).
	/// Format identique à celui que `SplatPaintTool` produit en runtime
	/// (parité éditeur ↔ client).
	bool WriteSplatMap(std::string_view outputRootDir, int32_t chunkX, int32_t chunkZ,
		const engine::world::terrain::SplatMap& splat, std::string& outError);

	/// Écrit `instances/water.bin` pour la scene fournie (M100.13). Crée le
	/// dossier `instances/` au besoin. Cohérent avec WriteTerrainChunk /
	/// WriteSplatMap.
	bool WriteWater(std::string_view outputRootDir,
		const engine::world::water::WaterScene& scene, std::string& outError);

	/// Écrit `instances/hazards.bin` (M100.16) à `outputRootDir/instances/hazards.bin`.
	/// Crée le dossier parent si nécessaire. Retourne false + outError sur erreur I/O.
	bool WriteHazards(std::string_view outputRootDir,
		const engine::world::hazard::HazardScene& scene,
		std::string& outError);
}
