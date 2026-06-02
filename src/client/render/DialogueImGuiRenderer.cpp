#include "src/client/render/DialogueImGuiRenderer.h"

#include "src/client/dialogue/DialoguePresenter.h"

#include <cstdio>

#if defined(_WIN32)
#	include "imgui.h"

namespace engine::render
{
	namespace
	{
		/// Hauteur en pixels réservée pour la barre de titre custom (nom + rôle + séparateur).
		constexpr float kTitleBarH = 52.0f;

		/// Hauteur en pixels d'un bouton de choix (ligne de base + marges ImGui).
		constexpr float kChoiceRowH = 40.0f;

		/// Espace réservé en bas de la fenêtre (légende raccourcis + marges).
		constexpr float kBottomPadH = 28.0f;
	}

	/// Dessine la fenêtre de dialogue PNJ (disposition B — parchemin centré).
	/// Implémente : positionnement centré, skin parchemin/or, zone texte scrollable
	/// avec word-wrap, métriques auto-scroll vers le presenter, détection scroll
	/// manuel, boutons de choix 1..5 avec raccourcis clavier 1..5, fermeture
	/// via la croix → Close(UserClose).
	/// \param presenter source de vérité du dialogue (logique pure).
	/// \param viewportWidth / viewportHeight dimensions de la surface ImGui en pixels.
	void DialogueImGuiRenderer::Render(engine::client::DialoguePresenter& presenter,
	                                   float viewportWidth, float viewportHeight)
	{
		using namespace engine::client;

		if (!presenter.IsActive() || presenter.CurrentNode() == nullptr)
			return;

		const DialogueNode& node = *presenter.CurrentNode();

		// --- Taille et position de la fenêtre (centrée) ---
		const ImVec2 winSize(560.0f, 460.0f);
		ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);
		ImGui::SetNextWindowPos(
			ImVec2(viewportWidth * 0.5f, viewportHeight * 0.5f),
			ImGuiCond_Always,
			ImVec2(0.5f, 0.5f));

		// --- Skin parchemin + cadre or ---
		ImGui::PushStyleColor(ImGuiCol_WindowBg,    ImVec4(0.91f, 0.86f, 0.75f, 0.98f));
		ImGui::PushStyleColor(ImGuiCol_Border,      ImVec4(0.78f, 0.64f, 0.29f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Text,        ImVec4(0.17f, 0.13f, 0.07f, 1.0f));
		// Boutons de choix : teinte bois clair
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.78f, 0.64f, 0.29f, 0.35f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.64f, 0.29f, 0.60f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.78f, 0.64f, 0.29f, 0.85f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);

		const ImGuiWindowFlags winFlags =
			  ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings;

		bool open = true;
		if (ImGui::Begin("##dialogue_pnj", &open, winFlags))
		{
			// --- Barre de titre custom : nom du PNJ + rôle ---
			ImGui::TextUnformatted(presenter.Npc().label.c_str());
			if (!presenter.Npc().role.empty())
			{
				ImGui::SameLine();
				ImGui::TextDisabled(" · %s", presenter.Npc().role.c_str());
			}
			ImGui::Separator();

			// --- Calcul de la hauteur de la zone texte ---
			// On réserve kChoiceRowH par choix + kBottomPadH pour la légende,
			// et kTitleBarH est déjà consommé par la barre de titre + séparateur.
			const float nbChoices    = static_cast<float>(node.choices.size());
			const float choicesBlock = kChoiceRowH * nbChoices + kBottomPadH;
			const float textHeight   = winSize.y - kTitleBarH - choicesBlock;
			const float safeTextH    = textHeight > 40.0f ? textHeight : 40.0f;

			// --- Zone texte (scrollable, word-wrap, scrollbar droite) ---
			ImGui::BeginChild("##dlg_text", ImVec2(0.0f, safeTextH), true,
			                  ImGuiWindowFlags_AlwaysVerticalScrollbar);
			{
				// Word-wrap ajusté à la largeur du child (moins la scrollbar).
				const float wrapWidth = ImGui::GetContentRegionAvail().x;
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);

				for (const DialogueLine& line : node.lines)
				{
					if (line.isCue)
					{
						// Didascalie : affichée en texte atténué et italique via TextDisabled.
						ImGui::TextDisabled("%s", line.text.c_str());
					}
					else
					{
						ImGui::TextUnformatted(line.text.c_str());
					}
					ImGui::Spacing();
				}

				ImGui::PopTextWrapPos();

				// --- Métriques auto-scroll → presenter ---
				// GetScrollMaxY() + GetWindowHeight() donne la hauteur totale du contenu.
				const float contentH = ImGui::GetScrollMaxY() + ImGui::GetWindowHeight();
				presenter.SetViewMetrics(contentH, ImGui::GetWindowHeight());

				// Détection d'un scroll manuel (molette sur le child).
				if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
					presenter.OnUserScroll(ImGui::GetScrollY());

				// Application de l'offset calculé par le presenter.
				if (presenter.AutoScrollEnabled())
					ImGui::SetScrollY(presenter.ScrollOffset());
			}
			ImGui::EndChild();

			// --- Zone réponses (2..5 choix, word-wrap, raccourcis 1..5) ---
			ImGui::Spacing();
			ImGui::TextDisabled("Vos réponses :");

			const float wrapW = ImGui::GetContentRegionAvail().x;
			const size_t choiceCount = node.choices.size();
			for (size_t i = 0; i < choiceCount; ++i)
			{
				// Libellé : numéro + icône optionnelle + texte du choix.
				// Le suffix ##choice<i> garantit l'unicité de l'ID ImGui.
				char label[512];
				if (!node.choices[i].icon.empty())
				{
					std::snprintf(label, sizeof(label), "%zu. %s %s##choice%zu",
					              i + 1u,
					              node.choices[i].icon.c_str(),
					              node.choices[i].text.c_str(),
					              i);
				}
				else
				{
					std::snprintf(label, sizeof(label), "%zu. %s##choice%zu",
					              i + 1u,
					              node.choices[i].text.c_str(),
					              i);
				}

				// Bouton pleine largeur avec word-wrap (PushTextWrapPos avant le bouton).
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
				const bool clicked = ImGui::Button(label, ImVec2(wrapW, 0.0f));
				ImGui::PopTextWrapPos();

				// Raccourcis clavier 1..5 (API ImGui moderne : ImGuiKey_1 + offset).
				// Limité à i < 5 pour ne pas dépasser ImGuiKey_5.
				bool keyed = false;
				if (i < 5u)
				{
					keyed = ImGui::IsKeyPressed(
						static_cast<ImGuiKey>(ImGuiKey_1 + static_cast<int>(i)));
				}

				if (clicked || keyed)
				{
					// presenter.SelectChoice peut fermer le dialogue ou naviguer.
					// On sort de la boucle immédiatement : l'état du node a changé.
					presenter.SelectChoice(i);
					break;
				}
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Touches 1..%zu pour sélectionner", choiceCount < 5u ? choiceCount : 5u);
		}
		ImGui::End();

		ImGui::PopStyleVar();          // WindowBorderSize
		ImGui::PopStyleColor(6);       // WindowBg, Border, Text, Button, ButtonHovered, ButtonActive

		// Fermeture via la croix de la fenêtre (ImGui::Begin a mis open=false).
		if (!open)
			presenter.Close(DialogueCloseReason::UserClose);
	}

} // namespace engine::render

#else // !_WIN32

namespace engine::render
{
	/// Stub non-Windows : le renderer ImGui n'existe pas hors Windows.
	void DialogueImGuiRenderer::Render(engine::client::DialoguePresenter&, float, float) {}
}

#endif // _WIN32
