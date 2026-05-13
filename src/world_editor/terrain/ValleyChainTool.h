#pragma once

#include "src/world_editor/core/CommandStack.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"

#include <cstddef>
#include <cstdint>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class TerrainDocument;

	/// Outil "Valley Chain" (M100.35). Jumeau de `MountainRangeTool` avec un
	/// `invert=true` côté rasterisation et un push de `ValleyChainCommand` à
	/// l'Apply. La séparation en deux classes (plutôt qu'un flag interne)
	/// est imposée par la spec pour que les deux outils aient leur propre
	/// état et leur propre point d'extension futur (preview bleue vs ambre,
	/// labels différents, raccourcis dédiés).
	///
	/// Contraintes thread/timing : main thread (mute `WorldEditorShell` /
	/// `CommandStack`).
	class ValleyChainTool
	{
	public:
		bool Init(CommandStack& stack, TerrainDocument& doc,
			const engine::core::Config& cfg);

		void Reset();

		void AddVertex(float worldX, float worldZ);
		void RemoveVertex(size_t idx);
		void MoveVertex(size_t idx, float worldX, float worldZ);
		void ToggleLoop();
		void SetGlobalParams(FlankProfile profile, uint32_t seed, float freq);

		void   SetActiveVertex(size_t idx) { m_activeVertex = idx; }
		size_t GetActiveVertex() const { return m_activeVertex; }

		/// Rasterise la polyline courante en mode soustractif (invert=true).
		SparseChunkDeltas BuildDeltas() const;

		bool Apply();
		void Cancel();

		const MacroPolylineParams& Params() const { return m_params; }
		MacroPolylineParams& MutableParams() { return m_params; }

		size_t VertexCount() const { return m_params.vertices.size(); }
		bool   HasPolyline() const { return !m_params.vertices.empty(); }

	private:
		CommandStack*               m_stack = nullptr;
		TerrainDocument*            m_doc   = nullptr;
		const engine::core::Config* m_cfg   = nullptr;
		MacroPolylineParams         m_params;
		size_t                      m_activeVertex = 0;
	};
}
