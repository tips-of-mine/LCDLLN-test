// src/world_editor/LakeTool.cpp
#include "src/world_editor/LakeTool.h"

#include "src/world_editor/AddLakeCommand.h"
#include "src/world_editor/CommandStack.h"

#include <memory>
#include <string>

namespace engine::editor::world
{
	bool LakeTool::Init(CommandStack& stack, WaterDocument& waterDoc) noexcept
	{
		m_stack = &stack;
		m_doc   = &waterDoc;
		return true;
	}

	void LakeTool::AddPoint(float worldX, float worldZ)
	{
		if (!m_doc) return;
		m_currentPoints.push_back({ worldX, m_currentWaterLevelY, worldZ });
	}

	void LakeTool::ClosePolygon()
	{
		if (!m_stack || !m_doc) return;
		if (m_currentPoints.size() < 3) return;

		engine::world::water::LakeInstance lake;
		lake.name        = "lake_" + std::to_string(m_doc->Get().lakes.size() + 1);
		lake.polygon     = m_currentPoints;
		lake.waterLevelY = m_currentWaterLevelY;
		lake.bottomColor = m_currentBottomColor;
		lake.turbidity   = m_currentTurbidity;

		m_stack->Push(std::make_unique<AddLakeCommand>(*m_doc, std::move(lake)));
		m_currentPoints.clear();
	}

	void LakeTool::Cancel() noexcept
	{
		m_currentPoints.clear();
	}
}
