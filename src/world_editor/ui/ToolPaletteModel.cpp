// ToolPaletteModel — implémentation. Voir ToolPaletteModel.h.

#include "src/world_editor/ui/ToolPaletteModel.h"

namespace engine::editor::world
{
	const std::vector<ToolPaletteGroup>& GetToolPaletteGroups()
	{
		// Construction unique au premier appel (pas d'état mutable ensuite).
		// L'ordre des familles et des outils est l'ordre d'affichage de la
		// palette ; il suit la progression type d'un mappeur (terrain → eau
		// → macro-génération → structures 3D).
		static const std::vector<ToolPaletteGroup> kGroups = {
			{ "Terrain", {
				ActiveTool::TerrainSculpt,
				ActiveTool::TerrainStamp,
				ActiveTool::SplatPaint,
			} },
			{ "Eau", {
				ActiveTool::Lake,
				ActiveTool::River,
				ActiveTool::RiverNetwork,
				ActiveTool::Coastline,
			} },
			{ "Macro", {
				ActiveTool::MountainRange,
				ActiveTool::ValleyChain,
				ActiveTool::HydraulicErosion,
				ActiveTool::ThermalWindErosion,
			} },
			{ "Structures", {
				ActiveTool::Cave,
				ActiveTool::Overhang,
				ActiveTool::Arch,
				ActiveTool::DungeonPortal,
				ActiveTool::Spline, // Roadmap-8 — routes/chemins
			} },
			// Roadmap-8 (audit 2026-06-05, thème 1.1) — famille Gameplay :
			// outils M100.16/M100.28 enfin câblés (zones typées + dangers).
			{ "Gameplay", {
				ActiveTool::GameplayZone,
				ActiveTool::Hazard,
			} },
		};
		return kGroups;
	}

	const char* ToolLabelFr(ActiveTool tool)
	{
		switch (tool)
		{
			case ActiveTool::None:               return "Aucun outil";
			case ActiveTool::TerrainSculpt:      return "Sculpture du terrain";
			case ActiveTool::TerrainStamp:       return "Tampon de terrain";
			case ActiveTool::SplatPaint:         return "Peinture de texture (sol)";
			case ActiveTool::Lake:               return "Lac";
			case ActiveTool::River:              return "Rivière";
			case ActiveTool::MountainRange:      return "Chaîne de montagnes";
			case ActiveTool::ValleyChain:        return "Chaîne de vallées";
			case ActiveTool::RiverNetwork:       return "Réseau fluvial";
			case ActiveTool::Coastline:          return "Littoral";
			case ActiveTool::HydraulicErosion:   return "Érosion hydraulique";
			case ActiveTool::ThermalWindErosion: return "Érosion thermique/vent";
			case ActiveTool::Cave:               return "Grotte";
			case ActiveTool::Overhang:           return "Surplomb";
			case ActiveTool::Arch:               return "Arche";
			case ActiveTool::DungeonPortal:      return "Portail de donjon";
			case ActiveTool::Spline:             return "Spline (route/chemin)";
			case ActiveTool::GameplayZone:       return "Zone de gameplay";
			case ActiveTool::Hazard:             return "Danger (piège)";
		}
		return "?";
	}

	const char* ToolActionId(ActiveTool tool)
	{
		switch (tool)
		{
			case ActiveTool::None:               return "";
			case ActiveTool::TerrainSculpt:      return "tool.terrain-sculpt";
			case ActiveTool::TerrainStamp:       return "tool.terrain-stamp";
			case ActiveTool::SplatPaint:         return "tool.splat-paint";
			case ActiveTool::Lake:               return "tool.lake";
			case ActiveTool::River:              return "tool.river";
			case ActiveTool::MountainRange:      return "tool.mountain-range";
			case ActiveTool::ValleyChain:        return "tool.valley-chain";
			case ActiveTool::RiverNetwork:       return "tool.river-network";
			case ActiveTool::Coastline:          return "tool.coastline";
			case ActiveTool::HydraulicErosion:   return "tool.hydraulic-erosion";
			case ActiveTool::ThermalWindErosion: return "tool.thermal-wind-erosion";
			case ActiveTool::Cave:               return "tool.cave";
			case ActiveTool::Overhang:           return "tool.overhang";
			case ActiveTool::Arch:               return "tool.arch";
			case ActiveTool::DungeonPortal:      return "tool.dungeon-portal";
			case ActiveTool::Spline:             return "tool.spline";
			case ActiveTool::GameplayZone:       return "tool.gameplay-zone";
			case ActiveTool::Hazard:             return "tool.hazard";
		}
		return "";
	}
}
