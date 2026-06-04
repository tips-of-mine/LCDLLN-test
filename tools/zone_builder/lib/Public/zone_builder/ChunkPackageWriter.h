#pragma once

#include <zone_builder/LayoutImporter.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::world::terrain { struct TerrainChunk; struct TerrainLodChain; struct SplatMap; }
namespace engine::world::water { struct WaterScene; }
namespace engine::routine { struct RoutineGraph; }
namespace engine::world::hazard { struct HazardVolume; }
namespace engine::world::instances { struct PropInstance; }
namespace engine::world::foliage { struct FoliageInstance; }
namespace engine::world::wind { struct WindZone; }
namespace engine::world::thermal { struct ShadeMap; }
namespace engine::world::zones { struct GameplayZone; }
namespace engine::world::spline { struct Spline; }

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

	/// Écrit `instances/water.bin` pour la scene fournie (M100.13, étendu
	/// M100.36). Crée le dossier `instances/` au besoin. Cohérent avec
	/// WriteTerrainChunk / WriteSplatMap.
	///
	/// \param seaLevelMeters Niveau de mer global de la zone (M100.36 ;
	///                       persisté dans la section ocean v2 du fichier).
	///                       Défaut 50.0f pour les zones legacy / les
	///                       contextes qui n'expriment pas un sea level.
	bool WriteWater(std::string_view outputRootDir,
		const engine::world::water::WaterScene& scene,
		float seaLevelMeters,
		std::string& outError);

	/// Écrit `routines.bin` (M101.3) dans `<outputRootDir>/chunks/chunk_<x>_<z>/`.
	/// Crée le dossier au besoin. Sérialise via `engine::routine::codec::
	/// EncodeRoutinesBin` (JSON canonique length-prefixé) ; format identique à
	/// celui que le client relit avec `DecodeRoutinesBin` (parité éditeur ↔
	/// client). Extension ADDITIVE du package : ne touche aucun segment existant.
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteRoutines(std::string_view outputRootDir, int32_t chunkX, int32_t chunkZ,
		const std::vector<engine::routine::RoutineGraph>& graphs, std::string& outError);

	/// Écrit `instances/hazards.bin` (M100.16) sous `<outputRootDir>/instances/`.
	/// Crée le dossier au besoin. Sérialise via `engine::world::hazard::
	/// SaveHazardsBin` (format header-only partagé avec le client : parité
	/// éditeur ↔ client). Fichier zone-level (pas par chunk).
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteHazards(std::string_view outputRootDir,
		const std::vector<engine::world::hazard::HazardVolume>& hazards, std::string& outError);

	/// Écrit `instances/props.bin` (M100.17) sous `<outputRootDir>/instances/`.
	/// Crée le dossier au besoin. Sérialise via `engine::world::instances::
	/// SavePropsBin` (format header-only partagé avec le client : parité éditeur
	/// ↔ client). Fichier zone-level.
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteProps(std::string_view outputRootDir,
		const std::vector<engine::world::instances::PropInstance>& props, std::string& outError);

	/// Écrit `foliage.bin` (M100.18) dans `<outputRootDir>/chunks/chunk_<x>_<z>/`.
	/// Crée le dossier au besoin. Sérialise via `engine::world::foliage::
	/// SaveFoliageBin` (header-only partagé client). Fichier par chunk.
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteFoliage(std::string_view outputRootDir, int32_t chunkX, int32_t chunkZ,
		const std::vector<engine::world::foliage::FoliageInstance>& items, std::string& outError);

	/// Écrit `instances/wind_zones.bin` (M100.20) sous `<outputRootDir>/instances/`.
	/// Crée le dossier au besoin. Sérialise via `engine::world::wind::
	/// SaveWindZonesBin` (header-only partagé client). Fichier zone-level.
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteWindZones(std::string_view outputRootDir,
		const std::vector<engine::world::wind::WindZone>& zones, std::string& outError);

	/// Écrit `shade.bin` (M100.27) dans `<outputRootDir>/chunks/chunk_<x>_<z>/`.
	/// Crée le dossier au besoin. Sérialise via `engine::world::thermal::
	/// SaveShadeMapBin` (header-only partagé client). Fichier par chunk.
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteShadeMap(std::string_view outputRootDir, int32_t chunkX, int32_t chunkZ,
		const engine::world::thermal::ShadeMap& shade, std::string& outError);

	/// Écrit `instances/zones.bin` (M100.28) sous `<outputRootDir>/instances/`.
	/// Crée le dossier au besoin. Sérialise via `engine::world::zones::
	/// SaveZonesBin` (header-only partagé client). Fichier zone-level.
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteZones(std::string_view outputRootDir,
		const std::vector<engine::world::zones::GameplayZone>& zones, std::string& outError);

	/// Écrit `instances/splines.bin` (M100.29) sous `<outputRootDir>/instances/`.
	/// Crée le dossier au besoin. Sérialise via `engine::world::spline::
	/// SaveSplinesBin` (header-only partagé client). Fichier zone-level.
	/// \return true si OK ; sinon `outError` est renseigné.
	bool WriteSplines(std::string_view outputRootDir,
		const std::vector<engine::world::spline::Spline>& splines, std::string& outError);
}
