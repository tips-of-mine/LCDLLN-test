#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/zone_presets/CustomizationApplier.h"
#include "src/world_editor/zone_presets/ZonePreset.h"

#include <memory>

namespace engine::editor::world
{
	class TerrainDocument;
}

namespace engine::editor::world::volumes
{
	class MeshInsertDocument;
	namespace caves    { class CaveCatalog; }
	namespace overhangs { class OverhangCatalog; }
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
	/// supportées (M100.46 incrément 2b).
	///
	/// Pour cet incrément, les types câblés end-to-end sont les
	/// **place_*** (cave, overhang, dungeon) dont les chemins de commande
	/// existent déjà (M100.40-43). Les 11 autres types (mountain_macro,
	/// valley_macro, sculpt_brush, splat_paint, lake_polygon, river_manual,
	/// coastline, river_network, hydraulic_erosion, thermal_wind_erosion,
	/// place_arch) demandent d'ajouter des chemins « headless » aux outils
	/// concernés — itérations suivantes (2c, 2d…).
	struct DispatchContext
	{
		engine::editor::world::TerrainDocument&                        terrain;
		engine::editor::world::volumes::MeshInsertDocument&            meshInserts;
		engine::editor::world::volumes::dungeons::DungeonPortalDocument& dungeonPortals;

		const engine::editor::world::volumes::caves::CaveCatalog&      caveCatalog;
		const engine::editor::world::volumes::overhangs::OverhangCatalog& overhangCatalog;
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
