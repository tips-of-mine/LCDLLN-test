// M100.28 — Rendu du panneau de l'outil Zone (ImGui guardé).

#include "src/world_editor/ZoneTool.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <cstdio>
#endif

namespace engine::editor::world
{
	void ZoneTool::Render()
	{
#if defined(_WIN32)
		using engine::world::zones::ZoneType;
		ImGui::TextUnformatted("Gameplay Zone");
		ImGui::Separator();

		const char* kTypes[] = { "SafeZone", "PvPZone", "RaidZone", "NoBuild", "QuestTrigger", "WeatherOverride" };
		int t = static_cast<int>(m_current.type);
		if (ImGui::Combo("Type", &t, kTypes, IM_ARRAYSIZE(kTypes))) m_current.type = static_cast<ZoneType>(t);

		char buf[96]; std::snprintf(buf, sizeof(buf), "%s", m_current.name.c_str());
		if (ImGui::InputText("Name", buf, sizeof(buf))) m_current.name = buf;

		ImGui::Text("Polygone : %d points", static_cast<int>(m_current.polygon.size()));
		if (ImGui::Button("Clear polygon")) Clear();

		if (m_current.type == ZoneType::WeatherOverride)
		{
			int w = static_cast<int>(m_current.weatherType);
			if (ImGui::InputInt("Override weather", &w) && w >= 0) m_current.weatherType = static_cast<uint32_t>(w);
			ImGui::SliderFloat("Blend T", &m_current.weatherBlendT, 0.0f, 1.0f);
			ImGui::InputFloat("Transition margin (m)", &m_current.transitionMarginMeters);
		}
		if (m_current.type == ZoneType::QuestTrigger)
		{
			int q = static_cast<int>(m_current.questId);
			if (ImGui::InputInt("Quest id", &q) && q >= 0) m_current.questId = static_cast<uint32_t>(q);
		}
#endif
	}
}
