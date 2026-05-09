// src/world_editor/world/RiverTool.cpp
#include "src/world_editor/world/RiverTool.h"

#include "src/world_editor/world/AddRiverCommand.h"
#include "src/world_editor/world/CommandStack.h"
#include "src/world_editor/world/TerrainDocument.h"
#include "src/client/world/WorldModel.h"
#include "src/client/world/terrain/TerrainChunk.h"

#include <memory>
#include <string>

namespace engine::editor::world
{
	bool RiverTool::Init(CommandStack& stack,
		WaterDocument& waterDoc, TerrainDocument& terrainDoc,
		const engine::core::Config& cfg) noexcept
	{
		m_stack       = &stack;
		m_doc         = &waterDoc;
		m_terrainDoc  = &terrainDoc;
		m_cfg         = &cfg;
		return true;
	}

	void RiverTool::AddNode(float worldX, float worldZ)
	{
		if (!m_doc) return;

		float y = 0.0f;
		if (m_terrainDoc && m_cfg)
		{
			const auto coord = engine::world::WorldToGlobalChunkCoord(worldX, worldZ);
			auto chunk = m_terrainDoc->EnsureLoaded(*m_cfg, coord.x, coord.z);
			if (chunk)
			{
				const auto bounds = engine::world::ChunkBounds(coord);
				const float localX = worldX - bounds.minX;
				const float localZ = worldZ - bounds.minZ;
				y = chunk->SampleHeight(localX, localZ);
			}
		}

		engine::world::water::RiverNode node;
		node.position    = engine::math::Vec3{ worldX, y, worldZ };
		node.widthMeters = m_defaultWidth;
		node.depthMeters = m_defaultDepth;
		m_currentNodes.push_back(node);
	}

	void RiverTool::EndSpline()
	{
		if (!m_stack || !m_doc) return;
		if (m_currentNodes.size() < 2) return;

		engine::world::water::RiverInstance river;
		river.name  = "river_" + std::to_string(m_doc->Get().rivers.size() + 1);
		river.nodes = m_currentNodes;

		m_stack->Push(std::make_unique<AddRiverCommand>(*m_doc, std::move(river)));
		m_currentNodes.clear();
	}

	void RiverTool::Cancel() noexcept
	{
		m_currentNodes.clear();
	}
}
