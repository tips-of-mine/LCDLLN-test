#include "src/world_editor/terrain/ValleyChainCommand.h"

namespace engine::editor::world
{
	ValleyChainCommand::ValleyChainCommand(TerrainDocument& doc,
		SparseChunkDeltas deltas)
		: MacroPolylineCommandBase(doc, std::move(deltas), "Valley Chain")
	{
	}
}
