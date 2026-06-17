#pragma once

// M100.34 — WorldEditorExporter : orchestrateur "Save Zone". Écrit une
// arborescence chunk-package complète à partir des données instances de la
// zone, en réutilisant les sérialiseurs header-only partagés avec le client
// (Save*Bin dans engine_core) — AUCUNE duplication de format, et pas de
// dépendance vers zone_builder_lib (évite un cycle ; le format est la même
// source de vérité que les writers offline).
//
// Périmètre v1 de l'exporteur : couches d'instances zone-level (props, hazards,
// interactives, zones, splines, wind zones) + par-chunk (foliage, shade map).
// Le terrain / splat / LODs restent produits par les writers existants de
// zone_builder_lib (WriteTerrainChunk/WriteSplatMap/WriteTerrainLods, couverts
// par leurs propres tests) ; ils ne sont pas ré-implémentés ici.
//
// Parité éditeur ↔ client : une zone exportée est relue à l'identique par les
// Load*Bin (cf. ExportZoneTests::FullRoundtrip).

#include <cstdint>
#include <string>
#include <vector>

#include "src/client/world/foliage/FoliageInstances.h"
#include "src/client/world/hazard/HazardVolumes.h"
#include "src/client/world/instances/Buildings.h"
#include "src/client/world/instances/PropInstances.h"
#include "src/client/world/interactive/InteractiveInstances.h"
#include "src/client/world/spline/SplineInstances.h"
#include "src/client/world/thermal/ShadeMap.h"
#include "src/client/world/wind/WindZones.h"
#include "src/client/world/zones/Zones.h"

namespace engine::editor::world
{
	/// Données d'un chunk pour l'export par-chunk.
	struct ChunkExportData
	{
		int32_t chunkX = 0;
		int32_t chunkZ = 0;
		std::vector<engine::world::foliage::FoliageInstance> foliage;
		engine::world::thermal::ShadeMap shade;
		bool hasShade = false; ///< true si `shade` doit être écrit.
	};

	/// Agrégat des données exportables d'une zone (couches d'instances).
	struct ZoneExportInputs
	{
		std::vector<engine::world::instances::PropInstance>            props;
		std::vector<engine::world::instances::BuildingPlacement>      buildings;
		std::vector<engine::world::hazard::HazardVolume>              hazards;
		std::vector<engine::world::interactive::InteractivePropInstance> interactives;
		std::vector<engine::world::zones::GameplayZone>              zones;
		std::vector<engine::world::spline::Spline>                   splines;
		std::vector<engine::world::wind::WindZone>                   windZones;
		std::vector<ChunkExportData>                                 chunks;
	};

	/// Écrit l'arborescence complète sous `outputDir` :
	///   <outputDir>/instances/{props,buildings,hazards,interactives,zones,splines,wind_zones}.bin
	///   <outputDir>/chunks/chunk_<x>_<z>/{foliage,shade}.bin
	/// Crée les dossiers au besoin. Retourne false + `outError` rempli au premier
    /// échec d'écriture.
	bool SaveZone(const std::string& outputDir, const ZoneExportInputs& inputs, std::string& outError);
}
