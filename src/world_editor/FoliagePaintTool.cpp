// M100.18 — Rendu du panneau de l'outil de peinture foliage (ImGui guardé).

#include "src/world_editor/FoliagePaintTool.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <cstdio>
#endif

namespace engine::editor::world
{
	void FoliagePaintTool::Render()
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Foliage Paint");
		ImGui::Separator();

		char buf[128];
		std::snprintf(buf, sizeof(buf), "%s", m_params.activeAssetId.c_str());
		if (ImGui::InputText("Asset", buf, sizeof(buf))) m_params.activeAssetId = buf;

		ImGui::SliderFloat("Brush radius (m)", &m_params.brushRadius, 0.5f, 30.0f);
		ImGui::SliderFloat("Density target", &m_params.densityTarget, 0.0f, 1.0f);
		ImGui::SliderFloat("Strength", &m_params.strength, 0.0f, 1.0f);
		ImGui::SliderFloat("Falloff", &m_params.falloff, 0.0f, 1.0f);
		ImGui::InputFloat("Poisson min radius (m)", &m_params.minRadius);

		int mode = static_cast<int>(m_params.mode);
		ImGui::RadioButton("Add density", &mode, 0); ImGui::SameLine();
		ImGui::RadioButton("Erase density", &mode, 1);
		m_params.mode = static_cast<FoliagePaintMode>(mode);
#endif
	}
}
