// src/world_editor/hazard/HazardDocument.cpp
#include "src/world_editor/hazard/HazardDocument.h"

namespace engine::editor::hazard
{
	size_t HazardDocument::Add(const engine::world::hazard::HazardInstance& hz)
	{
		m_scene.hazards.push_back(hz);
		return m_scene.hazards.size() - 1;
	}

	void HazardDocument::Remove(size_t index)
	{
		if (index >= m_scene.hazards.size()) return;
		m_scene.hazards.erase(m_scene.hazards.begin() + static_cast<std::ptrdiff_t>(index));
	}
}
