// R1-B (Task 4) — CharacterSheetImGuiRenderer implementation.

#include "src/client/render/CharacterSheetImGuiRenderer.h"

#include "src/client/ui_common/UIModel.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cctype>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		ImVec4 IV(const LnTheme::Rgba& c) { return ImVec4(c.r, c.g, c.b, c.a); }

		/// Transforme une cle de ressource snake_case en libelle affichable :
		/// chaque '_' devient une espace, la premiere lettre passe en majuscule,
		/// le reste est laisse tel quel (ex. "magie_base" -> "Magie base",
		/// "flamme_draconique" -> "Flamme draconique"). Pas de table de libelles
		/// codee en dur : on derive l'etiquette de la cle brute envoyee par le
		/// serveur. Cle vide -> libelle generique "Ressource".
		std::string PrettyResourceLabel(const std::string& key)
		{
			if (key.empty())
				return "Ressource";
			std::string out = key;
			std::replace(out.begin(), out.end(), '_', ' ');
			out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
			return out;
		}

		/// Ouvre une ligne du tableau 2 colonnes et ecrit le label en colonne 0,
		/// puis se positionne en colonne 1 ou l'appelant ecrit la valeur via
		/// \c ImGui::Text(...). Evite un wrapper variadique (cf. siblings qui
		/// appellent ImGui::Text directement dans les cellules).
		void BeginStatRow(const char* label)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
		}
	}

	void CharacterSheetImGuiRenderer::Render(const engine::client::UIModel& model)
	{
		if (!m_enabled)
			return;

		const auto& s = model.playerStats;

		// Geometrie : panneau ancre a gauche, 320x420, centre vertical.
		const float panelW = 320.f;
		const float panelH = 420.f;
		const float margin = 24.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		const float posX = margin;
		const float posY = std::max(0.f, (vpH - panelH) * 0.5f);
		(void)vpW;

		ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.95f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, IV(LnTheme::PanelBg(0.95f)));
		ImGui::PushStyleColor(ImGuiCol_Border,   IV(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
		if (ImGui::Begin("Personnage##ln_character_sheet", nullptr, flags))
		{
			if (!s.hasSheet)
			{
				// Feuille pas encore recue (pre enter-world ou paquet manquant).
				ImGui::TextDisabled("Statistiques indisponibles");
			}
			else
			{
				const ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg
					| ImGuiTableFlags_BordersInnerH;
				if (ImGui::BeginTable("##ln_character_sheet_stats", 2, tableFlags, ImVec2(0.f, 0.f)))
				{
					ImGui::TableSetupColumn("Stat",   ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Valeur", ImGuiTableColumnFlags_WidthFixed, 110.f);

					// Le serveur envoie range == 0 pour une classe melee pure :
					// precision et portee n'ont alors pas de sens, on affiche "-".
					// Tiret ASCII (pas d'em-dash) : la couverture de glyphe de la
					// police HUD pour l'em-dash n'est pas garantie, on reste sur "-".
					const bool melee = (s.range == 0.0f);
					const char* kDash = "-"; // tiret ASCII (toujours rendu)

					BeginStatRow("PV max");
					ImGui::Text("%u", static_cast<unsigned>(s.sheetMaxHealth));

					const std::string resLabel = PrettyResourceLabel(s.secondaryResourceKey);
					BeginStatRow(resLabel.c_str());
					ImGui::Text("%u", static_cast<unsigned>(s.secondaryResourceMax));

					BeginStatRow("Endurance");
					ImGui::Text("%u", static_cast<unsigned>(s.staminaMax));

					BeginStatRow("Degats");
					ImGui::Text("%u", static_cast<unsigned>(s.damage));

					BeginStatRow("Precision");
					if (melee)
						ImGui::TextUnformatted(kDash);
					else
						ImGui::Text("%.1f %%", s.accuracy);

					BeginStatRow("Portee");
					if (melee)
						ImGui::TextUnformatted(kDash);
					else
						ImGui::Text("%.1f m", s.range);

					BeginStatRow("Taux crit");
					ImGui::Text("%.1f %%", s.critRate);

					BeginStatRow("Mult crit");
					ImGui::Text("%.2f x", s.critMult);

					BeginStatRow("Vitesse marche");
					ImGui::Text("%.1f", s.speedWalk);

					BeginStatRow("Vitesse course");
					ImGui::Text("%.1f", s.speedRun);

					BeginStatRow("Vitesse sprint");
					ImGui::Text("%.1f", s.speedSprint);

					BeginStatRow("Perception");
					ImGui::Text("%.1f m", s.perception);

					BeginStatRow("Discretion");
					ImGui::Text("%.1f m", s.stealth);

					ImGui::EndTable();
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
	}
}

#else // !_WIN32

namespace engine::render
{
	void CharacterSheetImGuiRenderer::Render(const engine::client::UIModel&) {}
}

#endif
