// M100.20 — Rendu du panneau de l'outil WindZone (ImGui guardé).

#include "src/world_editor/WindZoneTool.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	void WindZoneTool::Render()
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Wind Zone (local override)");
		ImGui::Separator();
		ImGui::Text("Polygone : %d points", static_cast<int>(m_current.polygon.size()));
		if (ImGui::Button("Clear polygon")) Clear();
		ImGui::InputFloat("Direction X", &m_current.directionX);
		ImGui::InputFloat("Direction Z", &m_current.directionZ);
		ImGui::SliderFloat("Force (m/s)", &m_current.forceMps, 0.0f, 20.0f);
		ImGui::SliderFloat("Turbulence freq", &m_current.turbulenceFreq, 0.0f, 2.0f);
		ImGui::SliderFloat("Turbulence amp", &m_current.turbulenceAmp, 0.0f, 3.0f);
#endif
	}
}
