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
		}
		// Fallback générique pour outils futurs non encore mappés : carré
		// neutre, désactivé, tooltip standard "Bientôt disponible".
		return { 0xFF606060u, "?", "Bientôt disponible", false };
	}

	ToolIconStyle ToolbarIconAtlas::GetDeselect()
	{
		return { 0xFFB04040u, "X", "Désélectionner l'outil", true };
	}
}
