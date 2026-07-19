#include "src/world_editor/ui/ToolbarIconAtlas.h"

namespace engine::editor::world
{
	ToolIconStyle ToolbarIconAtlas::Get(ActiveTool tool)
	{
		switch (tool)
		{
			case ActiveTool::None:
				return GetDeselect();
			case ActiveTool::TerrainSculpt:
				return { 0xFF8C7B6Cu, "S", "Sculpter le relief",        true  };
			case ActiveTool::TerrainStamp:
				return { 0xFF8AA66Eu, "T", "Tamponner un stamp",        true  };
			case ActiveTool::SplatPaint:
				return { 0xFF6E8AA6u, "P", "Peindre les surfaces",      true  };
			case ActiveTool::Lake:
				return { 0xFF4A7FB4u, "L", "Tracer un lac",             true  };
			case ActiveTool::River:
				return { 0xFF386FA4u, "R", "Tracer une rivière",        true  };
			case ActiveTool::MountainRange:
				return { 0xFFAA7755u, "M", "Tracer une chaîne de montagnes", true };
			case ActiveTool::ValleyChain:
				return { 0xFF557099u, "V", "Tracer une vallée",         true  };
			case ActiveTool::RiverNetwork:
				return { 0xFF2D6AA0u, "N", "Générer un réseau de rivières (watershed)", true };
			case ActiveTool::Coastline:
				return { 0xFF1A3A55u, "C", "Éditer la côte et le niveau de mer",     true };
			case ActiveTool::HydraulicErosion:
				return { 0xFF335577u, "H", "Érosion hydraulique (particules)",      true };
			case ActiveTool::ThermalWindErosion:
				return { 0xFF7A6042u, "T", "Érosion thermique + éolienne",          true };
			case ActiveTool::Cave:
				return { 0xFF3A2A1Au, "G", "Placer une grotte (Phase 11)",          true };
			case ActiveTool::Overhang:
				return { 0xFF4A3A28u, "O", "Placer un surplomb rocheux (Phase 11)", true };
			case ActiveTool::Arch:
				return { 0xFF5A4A36u, "A", "Placer une arche naturelle (Phase 11)", true };
			case ActiveTool::DungeonPortal:
				return { 0xFF6A2E5Au, "D", "Placer un portail de donjon (Phase 11)", true };
			// Roadmap-8 (audit 2026-06-05, 1.1) — outils M100.16/28/29 câblés.
			case ActiveTool::Spline:
				return { 0xFF9A8A5Au, "S", "Tracer une spline (route/chemin)",      true };
			case ActiveTool::GameplayZone:
				return { 0xFF3A7A5Au, "Z", "Tracer une zone de gameplay",           true };
			case ActiveTool::Hazard:
				return { 0xFFB05A2Au, "!", "Poser un danger (piège)",               true };
		}
		// Fallback générique pour outils futurs non encore mappés : carré
		// neutre, désactivé, tooltip standard "Bientôt disponible".
		return { 0xFF606060u, "?", "Bientôt disponible", false };
	}

	ToolIconStyle ToolbarIconAtlas::GetDeselect()
	{
		return { 0xFFB04040u, "X", "Désélectionner l'outil", true };
	}

	ToolIconStyle ToolbarIconAtlas::GetForAction(std::string_view actionId)
	{
		// Palette distincte de celle des outils : verts/bleus « action »
		// plutôt que les bruns « terrain ». Lettres = initiale du verbe FR.
		if (actionId == "file.save")     return { 0xFF2F7D4Fu, "S", "Sauvegarder la carte courante", true };
		if (actionId == "edit.undo")     return { 0xFF4A6FA5u, "Z", "Annuler",   true };
		if (actionId == "edit.redo")     return { 0xFF4A8FA5u, "Y", "Rétablir",  true };
		if (actionId == "zone.validate") return { 0xFF8A7A2Du, "V", "Valider la zone", true };
		if (actionId == "zone.export")   return { 0xFF7A4A8Au, "E", "Exporter en runtime", true };
		// Fallback : même convention que les outils non mappés.
		return { 0xFF606060u, "?", "Bientôt disponible", false };
	}
}
