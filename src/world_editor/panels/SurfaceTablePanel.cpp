// src/world_editor/panels/SurfaceTablePanel.cpp
#include "src/world_editor/panels/SurfaceTablePanel.h"
#include "src/client/world/surface/SurfaceType.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Charge `surface_table.json` depuis `<contentRoot>/gameplay/`.
	/// Le caller passe déjà `paths.content` (typiquement `game/data`) — on
	/// ne re-préfixe donc PAS avec `game/data` (bug doublait le chemin :
	/// `game/data/game/data/gameplay/surface_table.json` → parse error).
	/// Effets de bord : remplit `m_table` (succès) et `m_status` (succès ou
	/// erreur). Appelée une fois par `WorldEditorShell::Init` (main thread).
	/// Le bouton Reload du panel rappelle `m_table.LoadFromJson(m_jsonPath, err)`
	/// avec le même path.
	void SurfaceTablePanel::LoadFromContentRoot(const std::filesystem::path& contentRoot)
	{
		m_jsonPath = contentRoot / "gameplay" / "surface_table.json";
		std::string err;
		if (m_table.LoadFromJson(m_jsonPath, err))
		{
			m_status = "Loaded \xE2\x9C\x93 (13 entries)";  // UTF-8 ✓
		}
		else
		{
			m_status = "Parse error: " + err;
		}
	}

	/// Rend la window ImGui "Surface Table" : ligne source + bouton Reload,
	/// ligne status (rouge si parse error), puis tableau 4 colonnes
	/// (SurfaceType / Speed / Audio step / Visual tag) listant les 13 surfaces.
	/// Aucun champ éditable. Effet de bord : ImGui state, éventuel
	/// `m_table.LoadFromJson` si Reload cliqué. Doit être appelée depuis le
	/// main thread, dans la phase ImGui (entre NewFrame et Render).
	void SurfaceTablePanel::Render()
	{
#if defined(_WIN32)
		if (!ImGui::Begin(GetName(), &m_visible))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("Source: %s", m_jsonPath.string().c_str());
		ImGui::SameLine();
		if (ImGui::Button("Reload"))
		{
			std::string err;
			if (m_table.LoadFromJson(m_jsonPath, err))
				m_status = "Reloaded \xE2\x9C\x93 (13 entries)";
			else
				m_status = "Parse error: " + err;
		}

		// Status (rouge si parse error).
		if (m_status.find("error") != std::string::npos)
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Status: %s", m_status.c_str());
		else
			ImGui::Text("Status: %s", m_status.c_str());

		ImGui::Separator();

		if (!m_table.IsLoaded())
		{
			ImGui::TextDisabled("(table non chargee)");
			ImGui::End();
			return;
		}

		if (ImGui::BeginTable("##surfaces", 4,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
		{
			ImGui::TableSetupColumn("SurfaceType", ImGuiTableColumnFlags_WidthFixed, 130.0f);
			ImGui::TableSetupColumn("Speed",       ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableSetupColumn("Audio step",  ImGuiTableColumnFlags_WidthFixed, 200.0f);
			ImGui::TableSetupColumn("Visual tag",  ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableHeadersRow();

			using namespace engine::world::surface;
			for (int i = 0; i < static_cast<int>(SurfaceType::_Count); ++i)
			{
				const auto t = static_cast<SurfaceType>(i);
				const auto& e = m_table.Get(t);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(std::string(ToString(t)).c_str());
				ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", e.baseSpeed);
				ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(e.audioStep.c_str());
				ImGui::TableSetColumnIndex(3);
				if (e.visualTag.empty())
					ImGui::TextDisabled("\xE2\x80\x94");  // em dash
				else
					ImGui::TextUnformatted(e.visualTag.c_str());
			}
			ImGui::EndTable();
		}

		ImGui::End();
#endif
	}
}
