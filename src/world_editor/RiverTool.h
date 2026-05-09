// engine/editor/world/RiverTool.h
#pragma once

#include "engine/editor/world/WaterDocument.h"
#include "engine/world/water/WaterSurfaces.h"

#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	class CommandStack;
	class TerrainDocument;

	/// Outil d'édition d'une rivière (M100.13). État : nodes en cours de
	/// construction. AddNode(xz) sample la heightmap via TerrainDocument
	/// pour fixer Y. EndSpline() commit comme nouveau river via AddRiverCommand.
	class RiverTool
	{
	public:
		/// Init avec la pile undo, le doc water partagé, le doc terrain
		/// (pour SampleHeight au AddNode), et la config (pour
		/// EnsureLoaded chunk).
		bool Init(CommandStack& stack,
			WaterDocument& waterDoc,
			TerrainDocument& terrainDoc,
			const engine::core::Config& cfg) noexcept;

		/// Ajoute un node à la rivière en cours.
		/// Y = TerrainDocument::EnsureLoaded(...).SampleHeight(localXZ).
		/// Si chunk pas chargeable → fallback Y=0.0.
		void AddNode(float worldX, float worldZ);

		/// Termine la spline et commit via AddRiverCommand. No-op si < 2 nodes.
		void EndSpline();

		void Cancel() noexcept;

		bool   HasActiveRiver() const noexcept { return !m_currentNodes.empty(); }
		size_t GetNodeCount()   const noexcept { return m_currentNodes.size(); }
		const std::vector<engine::world::water::RiverNode>& GetCurrentNodes() const { return m_currentNodes; }

		float& MutableDefaultWidth() noexcept { return m_defaultWidth; }
		float& MutableDefaultDepth() noexcept { return m_defaultDepth; }

	private:
		CommandStack*               m_stack       = nullptr;
		WaterDocument*              m_doc         = nullptr;
		TerrainDocument*            m_terrainDoc  = nullptr;
		const engine::core::Config* m_cfg         = nullptr;
		std::vector<engine::world::water::RiverNode> m_currentNodes;
		float m_defaultWidth = 4.0f;
		float m_defaultDepth = 1.0f;
	};
}
