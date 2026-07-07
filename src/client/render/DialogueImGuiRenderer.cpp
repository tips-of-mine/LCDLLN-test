#include "src/client/render/DialogueImGuiRenderer.h"

#include "src/client/dialogue/DialoguePresenter.h"
#include "src/client/quest/QuestTextCatalog.h"
#include "src/client/ui_common/UIModel.h"

#include <cstdio>
#include <string>

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

		// --- Skin parchemin foncé + cadre or ---
		ImGui::PushStyleColor(ImGuiCol_WindowBg,      ImVec4(0.13f, 0.10f, 0.07f, 0.97f)); // bois sombre
		ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0.80f, 0.66f, 0.33f, 1.0f));  // cadre or
		ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.92f, 0.86f, 0.72f, 1.0f));  // texte parchemin clair
		ImGui::PushStyleColor(ImGuiCol_TextDisabled,  ImVec4(0.66f, 0.58f, 0.42f, 1.0f));  // didascalies / labels atténués
		// Boutons de choix : bois sombre, surbrillance dorée.
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.28f, 0.22f, 0.13f, 0.85f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.35f, 0.19f, 0.95f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.58f, 0.45f, 0.23f, 1.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
		// Réponses alignées à gauche (et non centrées).
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));

		// Pas de barre de titre ImGui (la barre bleue) : on dessine notre propre
		// en-tête (nom + bouton fermer) dans le corps de la fenêtre.
		const ImGuiWindowFlags winFlags =
			  ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoSavedSettings;

		bool requestClose = false;
		if (ImGui::Begin("##dialogue_pnj", nullptr, winFlags))
		{
			// --- En-tête custom : nom du PNJ + bouton fermer aligné à droite ---
			ImGui::TextUnformatted(presenter.Npc().label.c_str());
			const float closeBtnW = ImGui::GetFrameHeight();
			ImGui::SameLine(ImGui::GetContentRegionMax().x - closeBtnW);
			if (ImGui::Button("X##dlg_close", ImVec2(closeBtnW, 0.0f)))
				requestClose = true;
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
				const DialogueChoice& choice = node.choices[i];
				// SP2 — distinction visuelle des choix liés à une QUÊTE : couleur de
				// bouton dédiée (doré = accepter, vert = rendre) + tag texte, pour les
				// séparer nettement des réponses de bavardage ordinaires.
				const bool isAcceptChoice = (choice.action == DialogueAction::AcceptQuest);
				const bool isTurnInChoice = (choice.action == DialogueAction::CompleteQuest);
				const bool isQuestChoice  = isAcceptChoice || isTurnInChoice || choice.questId >= 0;
				const char* questTag = isAcceptChoice ? "  [Nouvelle quête]"
				                     : isTurnInChoice ? "  [Rendre la quête]"
				                     : isQuestChoice  ? "  [Quête]" : "";

				// Libellé : numéro + icône optionnelle + texte du choix + tag quête.
				// Le suffix ##choice<i> garantit l'unicité de l'ID ImGui.
				char label[512];
				if (!choice.icon.empty())
				{
					std::snprintf(label, sizeof(label), "%zu. %s %s%s##choice%zu",
					              i + 1u,
					              choice.icon.c_str(),
					              choice.text.c_str(),
					              questTag,
					              i);
				}
				else
				{
					std::snprintf(label, sizeof(label), "%zu. %s%s##choice%zu",
					              i + 1u,
					              choice.text.c_str(),
					              questTag,
					              i);
				}

				// Surbrillance dédiée aux choix de quête (avant le bouton).
				int questColorsPushed = 0;
				if (isAcceptChoice)
				{
					ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.46f, 0.35f, 0.11f, 0.92f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.64f, 0.49f, 0.17f, 1.0f));
					questColorsPushed = 2;
				}
				else if (isTurnInChoice)
				{
					ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.16f, 0.36f, 0.18f, 0.92f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.52f, 0.26f, 1.0f));
					questColorsPushed = 2;
				}

				// Bouton pleine largeur avec word-wrap (PushTextWrapPos avant le bouton).
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
				const bool clicked = ImGui::Button(label, ImVec2(wrapW, 0.0f));
				ImGui::PopTextWrapPos();

				// Toujours dépiler les couleurs de quête avant tout break de boucle.
				if (questColorsPushed > 0)
					ImGui::PopStyleColor(questColorsPushed);

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

			// PR-B — Acceptation/rendu de quête DANS la conversation. On injecte
			// sous les réponses scriptées un bouton par entrée du panneau donneur
			// (UIModel.giverList, instantané status-aware du dernier Talk : le serveur
			// n'y met role 0 « Accepter » que si la quête est proposée, role 1
			// « Rendre » que si les étapes sont remplies). Remplace l'ancien panneau
			// donneur séparé (QuestImGuiRenderer, désormais masqué tant qu'un dialogue
			// est actif). Le clic déclenche le callback quête d'Engine
			// (SendQuestAccept/TurnInRequest) ; l'entrée disparaît au frame suivant
			// (ApplyQuestDelta purge le giverList au changement de statut). No-op si
			// non bindé (BindQuestGiver) ou si le giverList est vide.
			if (m_uiModelBinding != nullptr && m_textCatalog != nullptr && m_giverAction)
			{
				const engine::client::UIQuestGiverList& giverList =
					m_uiModelBinding->GetModel().giverList;
				if (!giverList.entries.empty())
				{
					ImGui::Spacing();
					ImGui::Separator();
					for (const engine::client::UIQuestGiverEntry& entry : giverList.entries)
					{
						const bool isTurnIn = (entry.role == 1u);
						const std::string title = m_textCatalog->Title(entry.questId);
						char qlabel[512];
						std::snprintf(qlabel, sizeof(qlabel), "%s : %s##giver_%s_%u",
						              isTurnIn ? "Rendre" : "Accepter",
						              title.c_str(),
						              entry.questId.c_str(),
						              static_cast<unsigned>(entry.role));

						// Même code couleur que les choix scriptés de quête : doré =
						// accepter, vert = rendre.
						if (isTurnIn)
						{
							ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.16f, 0.36f, 0.18f, 0.92f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.52f, 0.26f, 1.0f));
						}
						else
						{
							ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.46f, 0.35f, 0.11f, 0.92f));
							ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.64f, 0.49f, 0.17f, 1.0f));
						}
						ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapW);
						const bool qClicked = ImGui::Button(qlabel, ImVec2(wrapW, 0.0f));
						ImGui::PopTextWrapPos();
						ImGui::PopStyleColor(2);

						if (qClicked)
						{
							m_giverAction(entry.questId, entry.role);
							break; // le giverList peut être purgé ce frame -> on sort.
						}
					}
				}
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Touches 1..%zu pour sélectionner", choiceCount < 5u ? choiceCount : 5u);
		}
		ImGui::End();

		ImGui::PopStyleVar(2);         // WindowBorderSize, ButtonTextAlign
		ImGui::PopStyleColor(7);       // WindowBg, Border, Text, TextDisabled, Button, ButtonHovered, ButtonActive

		// Fermeture via notre bouton fermer custom.
		if (requestClose)
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
