#pragma once

#include "src/world_editor/terrain/MacroPolylineCommandBase.h"

namespace engine::editor::world
{
	/// Commande Mountain Range (M100.35). Sémantique additive : `Execute`
	/// ajoute les deltas positifs (hauteurs montent), `Undo` les soustrait.
	/// Hérite intégralement de `MacroPolylineCommandBase` en lui passant le
	/// label "Mountain Range".
	class MountainRangeCommand final : public MacroPolylineCommandBase
	{
	public:
		MountainRangeCommand(TerrainDocument& doc, SparseChunkDeltas deltas);
	};
}
