// M100.29 — Rendu du panneau de l'outil Spline (ImGui guardé).

#include "src/world_editor/SplineTool.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	void SplineTool::Render()
	{
#if defined(_WIN32)
		using engine::world::spline::SplineType;
		using engine::world::spline::SplineCurve;
		ImGui::TextUnformatted("Spline / Road");
		ImGui::Separator();

		const char* kTypes[] = { "Road", "Path", "Wall", "RiverRef" };
		int t = static_cast<int>(m_current.type);
		if (ImGui::Combo("Type", &t, kTypes, IM_ARRAYSIZE(kTypes))) m_current.type = static_cast<SplineType>(t);

		int c = static_cast<int>(m_current.curve);
		ImGui::RadioButton("Catmull-Rom", &c, 0); ImGui::SameLine();
		ImGui::RadioButton("Bezier", &c, 1);
		m_current.curve = static_cast<SplineCurve>(c);

		ImGui::Checkbox("Closed", &m_current.closed);
		ImGui::Text("Noeuds : %d", static_cast<int>(m_current.nodes.size()));
		if (ImGui::Button("Clear")) Clear();

		int layer = static_cast<int>(m_current.splatLayerIndex);
		if (ImGui::InputInt("Splat layer", &layer) && layer >= 0) m_current.splatLayerIndex = static_cast<uint32_t>(layer);
		ImGui::SliderFloat("Splat strength", &m_current.splatStrength, 0.0f, 1.0f);
		ImGui::InputFloat("Feather (m)", &m_current.splatFeatherMeters);
#endif
	}
}
