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
		}
		return "?";
	}
}
