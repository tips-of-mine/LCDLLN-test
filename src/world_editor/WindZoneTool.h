#pragma once

// M100.20 — Outil de zone de vent locale (polygone + overrides). Rendu ImGui
// guardé Windows. La sérialisation/évaluation vit dans WindZones/WindSystem.

#include <vector>

#include "src/client/world/wind/WindZones.h"
#include "src/shared/math/Math.h"

namespace engine::editor::world
{
	class WindZoneTool
	{
	public:
		engine::world::wind::WindZone& Current() { return m_current; }
		const engine::world::wind::WindZone& Current() const { return m_current; }
		void AddPoint(const engine::math::Vec3& p) { m_current.polygon.push_back(p); }
		void Clear() { m_current.polygon.clear(); }
		void Render();

	private:
		engine::world::wind::WindZone m_current;
	};
}
