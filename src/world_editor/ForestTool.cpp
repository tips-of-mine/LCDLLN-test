// M100.19 — Rendu du panneau de l'outil Forest (ImGui guardé).

#include "src/world_editor/ForestTool.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <cstdio>
#endif

namespace engine::editor::world
{
	void ForestTool::Render()
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Forest");
		ImGui::Separator();
		ImGui::Text("Polygone : %d points", static_cast<int>(m_polygon.size()));
		if (ImGui::Button("Clear polygon")) ClearPolygon();

		int seed = static_cast<int>(m_recipe.seed);
		if (ImGui::InputInt("Seed", &seed) && seed >= 0) m_recipe.seed = static_cast<uint64_t>(seed);

		ImGui::TextUnformatted("Recette :");
		for (size_t i = 0; i < m_recipe.entries.size(); ++i)
		{
			ForestRecipeEntry& e = m_recipe.entries[i];
			ImGui::PushID(static_cast<int>(i));
			char buf[96]; std::snprintf(buf, sizeof(buf), "%s", e.assetId.c_str());
			if (ImGui::InputText("asset", buf, sizeof(buf))) e.assetId = buf;
			ImGui::SameLine(); ImGui::SetNextItemWidth(80);
			ImGui::InputFloat("w", &e.weight);
			ImGui::SameLine(); ImGui::SetNextItemWidth(90);
			ImGui::InputFloat("d/m2", &e.densityPerM2);
			ImGui::PopID();
		}
		if (ImGui::Button("+ entry")) m_recipe.entries.push_back(ForestRecipeEntry{});
		// La génération (GenerateForest) + push FoliagePaintCommand est déclenchée
		// par le shell ; ce panneau n'édite que la recette/le polygone.
#endif
	}
}
