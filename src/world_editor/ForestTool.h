#pragma once

// M100.19 — Outil Forest (polygone + recette). Rendu ImGui guardé Windows ; la
// génération est la fonction pure GenerateForest (ForestFieldGen). Les instances
// produites sont posées via FoliagePaintCommand (M100.18).

#include <vector>

#include "src/shared/math/Math.h"
#include "src/world_editor/ForestRecipe.h"

namespace engine::editor::world
{
	class ForestTool
	{
	public:
		ForestRecipe& Recipe() { return m_recipe; }
		const ForestRecipe& Recipe() const { return m_recipe; }
		std::vector<engine::math::Vec3>& Polygon() { return m_polygon; }
		const std::vector<engine::math::Vec3>& Polygon() const { return m_polygon; }

		void AddPolygonPoint(const engine::math::Vec3& p) { m_polygon.push_back(p); }
		void ClearPolygon() { m_polygon.clear(); }

		void Render();

	private:
		ForestRecipe m_recipe;
		std::vector<engine::math::Vec3> m_polygon;
	};
}
