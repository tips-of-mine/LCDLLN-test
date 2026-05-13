#include "src/world_editor/water/ConsolidatedHeightGrid.h"

namespace engine::editor::world
{
	ConsolidatedHeightGrid ConsolidatedHeightGrid::MakeFlat(int w, int h, float y)
	{
		ConsolidatedHeightGrid g;
		g.width = w;
		g.height = h;
		g.cellSizeMeters = 1.0f;
		g.originCellX = 0;
		g.originCellZ = 0;
		g.heights.assign(static_cast<size_t>(w) * static_cast<size_t>(h), y);
		return g;
	}
}
