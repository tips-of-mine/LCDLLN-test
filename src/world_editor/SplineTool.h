#pragma once

// M100.29 — Outil spline (routes/chemins). Rendu ImGui guardé. L'échantillonnage
// et la peinture vivent dans SplineSampler / TerrainDocument.

#include "src/client/world/spline/SplineInstances.h"
#include "src/shared/math/Math.h"

namespace engine::editor::world
{
	class SplineTool
	{
	public:
		engine::world::spline::Spline& Current() { return m_current; }
		const engine::world::spline::Spline& Current() const { return m_current; }
		void AddNode(const engine::math::Vec3& p, float width = 6.0f)
		{
			engine::world::spline::SplineNode n; n.position = p; n.widthMeters = width;
			m_current.nodes.push_back(n);
		}
		void Clear() { m_current.nodes.clear(); }
		void Render();

	private:
		engine::world::spline::Spline m_current;
	};
}
