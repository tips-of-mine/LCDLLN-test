#pragma once

// M100.28 — Outil de zone de gameplay (polygone multi-types). Rendu ImGui guardé.
// La sérialisation/requêtes vivent dans Zones/ZoneQuery.

#include "src/client/world/zones/Zones.h"
#include "src/shared/math/Math.h"

namespace engine::editor::world
{
	class ZoneTool
	{
	public:
		engine::world::zones::GameplayZone& Current() { return m_current; }
		const engine::world::zones::GameplayZone& Current() const { return m_current; }
		void AddPoint(const engine::math::Vec3& p) { m_current.polygon.push_back(p); }
		void Clear() { m_current.polygon.clear(); }
		void Render();

	private:
		engine::world::zones::GameplayZone m_current;
	};
}
