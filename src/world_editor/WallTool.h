#pragma once

// M100.30 — Outil Wall : transforme une spline en mur (kit + hauteur).
// Header-only, rendu ImGui guardé Windows. Génération dans structures::* (pur).

#include "src/client/world/spline/SplineInstances.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world
{
	class WallTool
	{
	public:
		engine::world::spline::SplineKitData& Kit() { return m_kit; }
		const engine::world::spline::SplineKitData& Kit() const { return m_kit; }

		void Render()
		{
#if defined(_WIN32)
			ImGui::TextUnformatted("Wall / Palissade");
			ImGui::Separator();
			ImGui::InputFloat("Segment length (m)", &m_kit.segmentLengthMeters);
			ImGui::SliderFloat("Height (m)", &m_kit.heightMeters, 0.5f, 12.0f);
			ImGui::SliderFloat("Post spacing (m)", &m_kit.postSpacingMeters, 1.0f, 12.0f);
#endif
		}

	private:
		engine::world::spline::SplineKitData m_kit{ engine::world::spline::SplineKitMode::Wall };
	};
}
