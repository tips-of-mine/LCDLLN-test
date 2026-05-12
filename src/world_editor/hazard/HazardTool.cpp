// src/world_editor/hazard/HazardTool.cpp
#include "src/world_editor/hazard/HazardTool.h"
#include "src/world_editor/hazard/HazardDocument.h"

#include <limits>

namespace engine::editor::hazard
{
	size_t HazardTool::PlaceAt(engine::math::Vec3 worldPos)
	{
		if (!m_document) return std::numeric_limits<size_t>::max();
		auto instance = m_template;
		instance.position = worldPos;
		return m_document->Add(instance);
	}
}
