#pragma once

// M100.31 — Outil générateur d'hameau. Header-only, rendu ImGui guardé. La
// génération est GenerateHamlet (pur) ; la pose passe par PlacePropsCommand.

#include <vector>

#include "src/shared/math/Math.h"
#include "src/world_editor/HamletGen.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <cstdio>
#endif

namespace engine::editor::world
{
	class HamletGeneratorTool
	{
	public:
		HamletRecipe& Recipe() { return m_recipe; }
		const HamletRecipe& Recipe() const { return m_recipe; }
		std::vector<engine::math::Vec3>& Polygon() { return m_polygon; }
		void AddPoint(const engine::math::Vec3& p) { m_polygon.push_back(p); }
		void Clear() { m_polygon.clear(); }

		void Render()
		{
#if defined(_WIN32)
			ImGui::TextUnformatted("Hamlet Generator");
			ImGui::Separator();
			ImGui::Text("Polygone : %d points", static_cast<int>(m_polygon.size()));
			if (ImGui::Button("Clear polygon")) Clear();
			ImGui::InputInt("House count", &m_recipe.houseCount);
			ImGui::InputFloat("Min spacing (m)", &m_recipe.minSpacing);
			ImGui::Checkbox("Snap to road", &m_recipe.snapToRoad);
			int seed = static_cast<int>(m_recipe.seed);
			if (ImGui::InputInt("Seed", &seed) && seed >= 0) m_recipe.seed = static_cast<uint64_t>(seed);
#endif
		}

	private:
		HamletRecipe m_recipe;
		std::vector<engine::math::Vec3> m_polygon;
	};
}
