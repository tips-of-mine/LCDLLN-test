#include "src/client/render/QuestImGuiRenderer.h"

#include "src/client/quest/QuestUi.h"
#include "src/client/quest/QuestTextCatalog.h"
#include "src/client/ui_common/UIModel.h"
#include "src/shared/core/Config.h"
#include "src/client/render/LnTheme.h"

#include <algorithm>
#include <cstdint>

#if defined(_WIN32)
#	include "imgui.h"
#	include "src/client/render/LnThemeImGui.h"

namespace engine::render
{
	namespace
	{
		using LnTheme::ToImVec4;

		/// Statuts quête (miroir de `engine::client::QuestStatus` / `quests::QuestStatus`,
		/// wire uint8) — dupliqués ici en constantes locales pour ne pas tirer une
		/// dépendance supplémentaire depuis un simple renderer d'affichage.
		constexpr uint8_t kQuestStatusOffered = 1;
		constexpr uint8_t kQuestStatusActive = 2;
		constexpr uint8_t kQuestStatusReadyToTurnIn = 3;
	}

	void QuestImGuiRenderer::BindQuestUi(engine::client::QuestUiPresenter* presenter,
		const engine::client::QuestTextCatalog* textCatalog,
		const engine::client::UIModelBinding* uiModelBinding,
		const engine::core::Config* cfg)
	{
		m_presenter = presenter;
		m_textCatalog = textCatalog;
		m_uiModelBinding = uiModelBinding;
		m_cfg = cfg;
	}

	void QuestImGuiRenderer::Render(float viewportW, float viewportH, bool inWorldShard)
	{
		(void)viewportW;
		(void)viewportH;

		if (m_presenter == nullptr || m_textCatalog == nullptr || m_uiModelBinding == nullptr)
			return;

		// Pas de quêtes hors monde (pas de shard courant, donc pas d'état quête valide).
		if (!inWorldShard)
			return;

		RenderJournal();
		RenderTracker();
		RenderGiverPanel();
	}

	void QuestImGuiRenderer::RenderJournal()
	{
		const engine::client::QuestUiState& state = m_presenter->GetState();
		if (!state.layoutValid)
			return;

		const engine::client::QuestUiRect& bounds = state.journalPanelBounds;
		ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(bounds.width, bounds.height), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.90f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.90f)));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("Journal de quetes##ln_quest_journal", nullptr, flags))
		{
			// Liste : uniquement Active / ReadyToTurnIn (Offered exclu -- pas
			// encore acceptée, elle vit dans le panneau donneur tant qu'elle
			// n'est pas acceptée).
			ImGui::BeginChild("##ln_quest_journal_list", ImVec2(0.f, ImGui::GetContentRegionAvail().y * 0.45f), true);
			for (const engine::client::QuestJournalEntryView& entry : state.journalEntries)
			{
				if (entry.status != kQuestStatusActive && entry.status != kQuestStatusReadyToTurnIn)
					continue;

				const std::string title = m_textCatalog->Title(entry.questId);
				char label[256];
				std::snprintf(label, sizeof(label), "%s (%u/%u)##%s",
					title.c_str(), entry.completedSteps, entry.totalSteps, entry.questId.c_str());

				const bool readyToTurnIn = (entry.status == kQuestStatusReadyToTurnIn);
				if (readyToTurnIn)
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kSuccess));

				if (ImGui::Selectable(label, entry.selected))
				{
					m_presenter->SelectQuest(entry.questId);
				}

				if (readyToTurnIn)
					ImGui::PopStyleColor();
			}
			ImGui::EndChild();

			ImGui::Separator();

			// Détail : titre / description / étapes (StepLabel) / récompenses.
			// Textes issus du QuestTextCatalog (Task 1) -- jamais synthétisés ici.
			ImGui::BeginChild("##ln_quest_journal_detail", ImVec2(0.f, 0.f), false);
			if (!state.selectedQuestId.empty())
			{
				const std::string title = m_textCatalog->Title(state.selectedQuestId);
				const std::string description = m_textCatalog->Description(state.selectedQuestId);

				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kAccent));
				ImGui::TextWrapped("%s", title.c_str());
				ImGui::PopStyleColor();

				if (!description.empty())
				{
					ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
					ImGui::TextUnformatted(description.c_str());
					ImGui::PopTextWrapPos();
				}

				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
				ImGui::TextUnformatted("Etapes :");
				ImGui::PopStyleColor();
				for (size_t i = 0; i < state.selectedQuestSteps.size(); ++i)
				{
					const engine::client::QuestStepView& step = state.selectedQuestSteps[i];
					const std::string stepLabel = m_textCatalog->StepLabel(
						state.selectedQuestId, i, step.currentCount, step.requiredCount);
					if (step.completed)
						ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kSuccess));
					ImGui::BulletText("%s (%u/%u)", stepLabel.c_str(), step.currentCount, step.requiredCount);
					if (step.completed)
						ImGui::PopStyleColor();
				}

				// Récompenses : lues directement sur le modèle (le QuestUiState du
				// presenter n'expose pas les récompenses, seulement les étapes).
				const engine::client::UIModel& model = m_uiModelBinding->GetModel();
				for (const engine::client::UIQuestEntry& quest : model.quests)
				{
					if (quest.questId != state.selectedQuestId)
						continue;

					if (quest.rewardExperience > 0 || quest.rewardGold > 0 || !quest.rewardItems.empty())
					{
						ImGui::Spacing();
						ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
						ImGui::TextUnformatted("Recompenses :");
						ImGui::PopStyleColor();
						if (quest.rewardExperience > 0)
							ImGui::BulletText("%u XP", quest.rewardExperience);
						if (quest.rewardGold > 0)
							ImGui::BulletText("%u or", quest.rewardGold);
						for (const auto& item : quest.rewardItems)
						{
							ImGui::BulletText("Objet #%u x%u", item.itemId, item.quantity);
						}
					}
					break;
				}
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
				ImGui::TextUnformatted("Aucune quete selectionnee.");
				ImGui::PopStyleColor();
			}
			ImGui::EndChild();
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
	}

	void QuestImGuiRenderer::RenderTracker()
	{
		const engine::client::QuestUiState& state = m_presenter->GetState();
		if (!state.layoutValid || state.trackerSteps.empty())
			return;

		const engine::client::QuestUiRect& bounds = state.trackerBounds;
		ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(bounds.width, bounds.height), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.78f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.78f)));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoInputs;

		if (ImGui::Begin("##ln_quest_tracker", nullptr, flags))
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kMuted));
			ImGui::TextUnformatted("Suivi de quetes");
			ImGui::PopStyleColor();
			ImGui::Separator();

			for (const engine::client::QuestStepView& step : state.trackerSteps)
			{
				if (step.completed)
					ImGui::PushStyleColor(ImGuiCol_Text, ToImVec4(LnTheme::kSuccess));
				ImGui::TextWrapped("%s (%u/%u)", step.label.c_str(), step.currentCount, step.requiredCount);
				if (step.completed)
					ImGui::PopStyleColor();
			}
		}
		ImGui::End();

		ImGui::PopStyleColor(2);
	}

	void QuestImGuiRenderer::RenderGiverPanel()
	{
		const engine::client::UIModel& model = m_uiModelBinding->GetModel();
		const engine::client::UIQuestGiverList& giverList = model.giverList;
		if (giverList.entries.empty())
			return;

		ImGui::SetNextWindowPos(ImVec2(0.f, 0.f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.f));
		ImGui::SetNextWindowBgAlpha(0.92f);

		ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(LnTheme::PanelBg(0.92f)));
		ImGui::PushStyleColor(ImGuiCol_Border, ToImVec4(LnTheme::kBorder));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("Quetes du PNJ##ln_quest_giver_panel", nullptr, flags))
		{
			for (const engine::client::UIQuestGiverEntry& entry : giverList.entries)
			{
				const std::string title = m_textCatalog->Title(entry.questId);
				ImGui::TextUnformatted(title.c_str());
				ImGui::SameLine();

				// role 0 = offer (pas encore acceptée) -> bouton Accepter.
				// role 1 = turnin (étapes remplies, à rendre) -> bouton Terminer.
				if (entry.role == 0)
				{
					char buttonId[160];
					std::snprintf(buttonId, sizeof(buttonId), "Accepter##accept_%s", entry.questId.c_str());
					if (ImGui::SmallButton(buttonId) && m_giverAction)
					{
						m_giverAction(entry.questId, entry.role);
					}
				}
				else
				{
					char buttonId[160];
					std::snprintf(buttonId, sizeof(buttonId), "Terminer##turnin_%s", entry.questId.c_str());
					if (ImGui::SmallButton(buttonId) && m_giverAction)
					{
						m_giverAction(entry.questId, entry.role);
					}
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
	void QuestImGuiRenderer::BindQuestUi(engine::client::QuestUiPresenter*,
		const engine::client::QuestTextCatalog*,
		const engine::client::UIModelBinding*,
		const engine::core::Config*) {}
	void QuestImGuiRenderer::Render(float, float, bool) {}
}

#endif
