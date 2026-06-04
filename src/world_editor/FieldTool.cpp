// M100.19 — Rendu du panneau de l'outil Field (ImGui guardé).

#include "src/world_editor/FieldTool.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	void FieldTool::Render()
	{
#if defined(_WIN32)
		ImGui::TextUnformatted("Field");
		ImGui::Separator();

		int crop = static_cast<int>(m_crop);
		ImGui::RadioButton("Wheat", &crop, 0); ImGui::SameLine();
		ImGui::RadioButton("Corn", &crop, 1);
		m_crop = static_cast<FieldCrop>(crop);

		ImGui::SliderFloat("Spacing (m)", &m_params.spacing, 0.2f, 2.0f);
		ImGui::SliderFloat("Rotation (deg)", &m_params.rotationDeg, 0.0f, 360.0f);
		ImGui::InputFloat("Width (m)", &m_params.width);
		ImGui::InputFloat("Depth (m)", &m_params.depth);
		ImGui::Checkbox("Auto-tag splat layer (WheatField/CornField)", &m_autoTagSplat);
		// Génération (GenerateField) + push FoliagePaintCommand déclenchée par le shell.
#endif
	}
}
