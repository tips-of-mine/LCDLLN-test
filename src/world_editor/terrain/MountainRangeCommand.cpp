#include "src/world_editor/terrain/MountainRangeCommand.h"

namespace engine::editor::world
{
	MountainRangeCommand::MountainRangeCommand(TerrainDocument& doc,
		SparseChunkDeltas deltas)
		: MacroPolylineCommandBase(doc, std::move(deltas), "Mountain Range")
	{
	}
}
