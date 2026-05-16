#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/zone_presets/CustomizationApplier.h"
#include "src/world_editor/zone_presets/ZonePreset.h"

#include <memory>

namespace engine::core
{
	class Config;
}

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
	/// Contexte fourni au dispatcher : refs sur les documents, catalogs et
	/// `Config` nécessaires pour construire les commandes (M100.46 incréments
	/// 2b + 2c + 2d + 2e).
	///
	/// Types câblés end-to-end (12/14 après incrément 2e) :
	///   - **place_*** (cave, overhang, arch, dungeon) — 2b/2c.
	///   - **mountain_macro / valley_macro** — 2d via `RasterizeMacroPolyline`.
	///   - **lake_polygon / river_manual** — 2d via Add*Command plain data.
	///   - **hydraulic_erosion / thermal_wind_erosion / river_network /
	///     coastline** — 2e via assemblage `ConsolidatedHeightGrid` +
	///     simulations pures (`RunHydraulicOnGrid`, `RunThermalSimulation`,
	///     `RunWindSimulation`, `RunWatershedOnGrid`,
	///     `ComputeCoastlineSmoothing/CliffsDeltas`).
	///
	/// Restent **`sculpt_brush`** et **`splat_paint`** comme `Unsupported` :
	/// les 8 presets livrés ne les utilisent pas, et leurs ICommand sont
	/// des actions ponctuelles non destinées à du batch déterministe.
	/// Câblage repoussé jusqu'à ce qu'un preset les exige.
	///
	/// Le `config` est requis par les 4 ops simulation (lecture des chunks
	/// via `TerrainDocument::EnsureLoaded`). Si `config == nullptr` ces 4
	/// ops renvoient `Failed` avec un LOG_WARN ; les 10 autres ops ne le
	/// consultent pas.
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

		/// Pointeur nullable. Requis pour les 4 ops sim (hydraulic /
		/// thermal_wind / river_network / coastline) qui chargent des chunks
		/// terrain via `EnsureLoaded(*config, ...)`. Null → ces ops failed.
		const engine::core::Config*                                    config = nullptr;
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
