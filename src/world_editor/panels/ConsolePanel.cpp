#include "src/world_editor/panels/ConsolePanel.h"

#include "src/world_editor/console/EditorLogSink.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <string>
#include <vector>

namespace engine::editor::world::panels
{
#if defined(_WIN32)
	namespace
	{
		/// Couleur du texte selon le niveau de log (lisibilité Console).
		ImVec4 ColorForLevel(engine::core::LogLevel lvl)
		{
			using L = engine::core::LogLevel;
			switch (lvl)
			{
				case L::Trace: return ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
				case L::Debug: return ImVec4(0.60f, 0.65f, 0.85f, 1.0f);
				case L::Info:  return ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
				case L::Warn:  return ImVec4(1.00f, 0.80f, 0.30f, 1.0f);
				case L::Error: return ImVec4(1.00f, 0.40f, 0.40f, 1.0f);
				case L::Fatal: return ImVec4(1.00f, 0.20f, 0.20f, 1.0f);
				default:       return ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
			}
		}
	}
#endif

	void ConsolePanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Console", &m_visible))
		{
			const char* levels[] = { "Trace", "Debug", "Info", "Warn", "Error" };
			ImGui::SetNextItemWidth(110.0f);
			ImGui::Combo("Niveau min", &m_minLevelIdx, levels, 5);
			ImGui::SameLine();
			ImGui::Checkbox("Auto-scroll", &m_autoScroll);
			ImGui::SameLine();
			if (ImGui::Button("Effacer"))
			{
				console::EditorLogSink::Instance().Clear();
			}
			ImGui::Separator();

			ImGui::BeginChild("##console_log", ImVec2(0.0f, 0.0f), false,
				ImGuiWindowFlags_HorizontalScrollbar);
			const auto minLevel = static_cast<engine::core::LogLevel>(m_minLevelIdx);
			const std::vector<console::LogEntry> entries =
				console::EditorLogSink::Instance().Snapshot(minLevel);
			for (const console::LogEntry& e : entries)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ColorForLevel(e.level));
				ImGui::TextUnformatted(("[" + e.subsystem + "] " + e.message).c_str());
				ImGui::PopStyleColor();
			}
			if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
			{
				ImGui::SetScrollHereY(1.0f);
			}
			ImGui::EndChild();
		}
		ImGui::End();
#endif
	}
}
