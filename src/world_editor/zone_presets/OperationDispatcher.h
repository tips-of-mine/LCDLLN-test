#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/zone_presets/CustomizationApplier.h"
#include "src/world_editor/zone_presets/ZonePreset.h"

#include <memory>

namespace engine::editor::world
{
	class TerrainDocument;
	class WaterDocument;
}

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
	namespace caves    { class CaveCatalog; }
	namespace overhangs { class OverhangCatalog; }
	namespace arches   { class ArchCatalog; }
	namespace dungeons
	{
		class DungeonPortalDocument;
		class DungeonCatalog;
	}
}

namespace engine::editor::world::zone_presets
{
	/// Contexte fourni au dispatcher : refs sur les documents et catalogs
	/// nécessaires pour construire les commandes des opérations
	/// supportées (M100.46 incréments 2b + 2d).
	///
	/// Types câblés end-to-end :
	///   - **place_*** (cave, overhang, arch, dungeon) — incrément 2b/2c.
	///   - **mountain_macro / valley_macro** — incrément 2d via
	///     `RasterizeMacroPolyline` + `MountainRangeCommand / ValleyChainCommand`.
	///   - **lake_polygon / river_manual** — incrément 2d via
	///     `AddLakeCommand / AddRiverCommand` (path direct, structs plain data).
	///
	/// Types encore non câblés (incréments 2e+, demandent d'extraire la
	/// simulation des Tools UI ou de capturer un snapshot d'état pré-action) :
	/// `coastline`, `river_network`, `hydraulic_erosion`,
	/// `thermal_wind_erosion`. Ces 4 types restent gracieusement
	/// **Unsupported** + LOG_INFO.
	struct DispatchContext
	{
		engine::editor::world::TerrainDocument&                        terrain;
		engine::editor::world::WaterDocument&                          water;
		engine::editor::world::volumes::MeshInsertDocument&            meshInserts;
		engine::editor::world::volumes::dungeons::DungeonPortalDocument& dungeonPortals;

		const engine::editor::world::volumes::caves::CaveCatalog&      caveCatalog;
		const engine::editor::world::volumes::overhangs::OverhangCatalog& overhangCatalog;
		const engine::editor::world::volumes::arches::ArchCatalog&     archCatalog;
		const engine::editor::world::volumes::dungeons::DungeonCatalog& dungeonCatalog;
	};

	/// Résultat d'un dispatch.
	enum class DispatchResult : uint8_t
	{
		Ok        = 0,  ///< commande créée
		Unsupported = 1, ///< type connu mais pas encore câblé en MVP
		Failed    = 2,  ///< params manquants / catalog entry introuvable
	};

	/// Construit la commande correspondant à `op`, paramètres typés
	/// extraits de `op.rawJson` + customisation appliquée. La commande
	/// n'est PAS poussée sur le stack — c'est le rôle du `ZonePresetExecutor`.
	///
	/// \param op       opération source (`type`, `rawJson`, `toolPresetId`,
	///                 `affectedBy`).
	/// \param custom   curseurs globaux relief/eau/sécheresse + seed.
	/// \param ctx      références documents + catalogs.
	/// \param outCmd   reçoit la commande (vide si non-OK).
	/// \return         statut détaillé pour le logging côté executor.
	DispatchResult DispatchOperation(const ZonePresetOperation& op,
		const CustomizationParams& custom,
		const DispatchContext& ctx,
		std::unique_ptr<engine::editor::world::ICommand>& outCmd);
}
