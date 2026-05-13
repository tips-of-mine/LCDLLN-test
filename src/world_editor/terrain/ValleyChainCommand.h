#pragma once

#include "src/world_editor/terrain/MacroPolylineCommandBase.h"

namespace engine::editor::world
{
	/// Commande Valley Chain (M100.35). Sémantique soustractive : la
	/// rasterisation a été faite avec `invert=true` côté outil, donc les
	/// deltas stockés sont déjà négatifs ; `Execute` les applique, `Undo`
	/// les retire. Label "Valley Chain".
	class ValleyChainCommand final : public MacroPolylineCommandBase
	{
	public:
		ValleyChainCommand(TerrainDocument& doc, SparseChunkDeltas deltas);
	};
}
