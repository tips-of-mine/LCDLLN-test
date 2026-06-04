#pragma once

// M100.30 — Outil Bridge : transforme une spline en pont (kit). Header-only,
// rendu ImGui guardé Windows. La génération vit dans structures::* (pur).

#include "src/client/world/spline/SplineInstances.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	class BridgeTool
	{
	public:
		engine::world::spline::SplineKitData& Kit() { return m_kit; }
		const engine::world::spline::SplineKitData& Kit() const { return m_kit; }

		void Render()
		{
#if defined(_WIN32)
			ImGui::TextUnformatted("Bridge");
			ImGui::Separator();
			ImGui::InputFloat("Segment length (m)", &m_kit.segmentLengthMeters);
			int ym = static_cast<int>(m_kit.yMode);
			ImGui::RadioButton("Y constant", &ym, 0); ImGui::SameLine();
			ImGui::RadioButton("Y follow spline", &ym, 1);
			m_kit.yMode = static_cast<uint32_t>(ym);
			ImGui::InputFloat("Y constant", &m_kit.yConstant);
			ImGui::SliderFloat("Width (m)", &m_kit.widthMeters, 1.0f, 20.0f);
#endif
		}

	private:
		engine::world::spline::SplineKitData m_kit{ engine::world::spline::SplineKitMode::Bridge };
	};
}
