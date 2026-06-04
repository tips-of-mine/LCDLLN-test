// M100.16 — Rendu du panneau de propriétés de l'outil Hazard (ImGui guardé).

#include "src/world_editor/HazardTool.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	void HazardTool::Render()
	{
#if defined(_WIN32)
		using engine::world::hazard::EscapeMode;
		using engine::world::hazard::HazardShape;
		using engine::world::hazard::HazardType;

		ImGui::TextUnformatted("Hazard");
		ImGui::Separator();

		const char* kTypes[] = { "Quicksand", "Bog", "Tar", "LavaSurface" };
		int typeIdx = static_cast<int>(m_params.type);
		if (ImGui::Combo("Type", &typeIdx, kTypes, IM_ARRAYSIZE(kTypes)))
		{
			SetType(static_cast<HazardType>(typeIdx));
		}

		int shapeIdx = static_cast<int>(m_params.shape);
		ImGui::RadioButton("Box", &shapeIdx, 0); ImGui::SameLine();
		ImGui::RadioButton("Cylinder", &shapeIdx, 1);
		m_params.shape = static_cast<HazardShape>(shapeIdx);

		if (m_params.shape == HazardShape::Cylinder)
		{
			ImGui::InputFloat("Radius (m)", &m_params.cylRadius);
			ImGui::InputFloat("Height (m)", &m_params.cylHeight);
		}
		else
		{
			float ext[3] = { m_params.boxHalfExtents.x, m_params.boxHalfExtents.y, m_params.boxHalfExtents.z };
			if (ImGui::InputFloat3("Half extents (m)", ext))
			{
				m_params.boxHalfExtents.x = ext[0];
				m_params.boxHalfExtents.y = ext[1];
				m_params.boxHalfExtents.z = ext[2];
			}
		}

		ImGui::InputFloat("Sink rate (m/s)", &m_params.sinkRateMps);
		ImGui::InputFloat("Max depth (m)", &m_params.maxDepthMeters);
		ImGui::InputFloat("Slowdown x", &m_params.slowdownMul);

		int escIdx = static_cast<int>(m_params.escapeMode);
		const char* kEscapes[] = { "None", "MashButton", "LateralMove", "MashButton+Item" };
		if (ImGui::Combo("Escape", &escIdx, kEscapes, IM_ARRAYSIZE(kEscapes)))
		{
			m_params.escapeMode = static_cast<EscapeMode>(escIdx);
		}
		if (m_params.escapeMode == EscapeMode::MashButtonRequireItem)
		{
			int item = static_cast<int>(m_params.requiredItemId);
			if (ImGui::InputInt("Required item id", &item) && item >= 0)
				m_params.requiredItemId = static_cast<uint32_t>(item);
		}
#endif
	}
}
