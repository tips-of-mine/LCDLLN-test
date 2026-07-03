#include "src/world_editor/panels/QuestEditorPanel.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	using engine::editor::world::quests::EditedQuest;
	using engine::editor::world::quests::EditedStep;
	using engine::editor::world::quests::EditedRewardItem;

	void QuestEditorPanel::EnsureLoaded()
	{
		if (m_loaded) return;
		m_loaded = true;
		if (!m_io)
		{
			m_status = "Erreur : QuestEditIo non initialise.";
			return;
		}
		std::string err;
		if (m_io->Load(m_contentRoot, m_quests, err))
		{
			m_status = std::to_string(m_quests.size()) + " quete(s) chargee(s).";
		}
		else
		{
			m_status = "Echec chargement : " + err;
		}
	}

	/// Copie la quête sélectionnée (`m_selected`) dans les buffers d'édition du
	/// formulaire (id, giver, turnIn, prérequis, **exclusions**, étapes,
	/// récompenses, **re-réalisation EXT-2 : mode/cooldown/autoComplete**,
	/// textes). No-op si aucune sélection valide.
	/// Effet de bord : écrase tous les `m_*Buffer` du panneau. Main thread (ImGui).
	void QuestEditorPanel::LoadBuffersFromSelected()
	{
		if (m_selected < 0 || m_selected >= static_cast<int>(m_quests.size())) return;
		const EditedQuest& q = m_quests[m_selected];
		std::snprintf(m_idBuf, sizeof(m_idBuf), "%s", q.id.c_str());
		std::snprintf(m_giverBuf, sizeof(m_giverBuf), "%s", q.giver.c_str());
		std::snprintf(m_turnInBuf, sizeof(m_turnInBuf), "%s", q.turnIn.c_str());
		m_prereqBuffer = q.prereqs;
		m_excludesBuffer = q.excludes;
		m_stepsBuffer = q.steps;
		m_rewardXpBuffer = q.rewardXp;
		m_rewardGoldBuffer = q.rewardGold;
		m_rewardItemsBuffer = q.rewardItems;
		m_repeatModeBuffer = q.repeatMode;
		m_cooldownHoursBuffer = q.cooldownHours;
		m_autoCompleteBuffer = q.autoComplete;
		std::snprintf(m_titleBuf, sizeof(m_titleBuf), "%s", q.title.c_str());
		std::snprintf(m_descriptionBuf, sizeof(m_descriptionBuf), "%s", q.description.c_str());
		m_stepLabelsBuffer = q.stepLabels;
		m_stepLabelsBuffer.resize(m_stepsBuffer.size());
	}

	/// Construit un `EditedQuest` à partir des buffers d'édition courants
	/// (opération inverse de \ref LoadBuffersFromSelected), incluant les
	/// **exclusions** (`m_excludesBuffer`) et la **re-réalisation EXT-2**
	/// (`m_repeatModeBuffer`/`m_cooldownHoursBuffer`/`m_autoCompleteBuffer`).
	/// Pur (ne modifie aucun état du panneau).
	EditedQuest QuestEditorPanel::BuildQuestFromBuffers() const
	{
		EditedQuest q;
		q.id = m_idBuf;
		q.giver = m_giverBuf;
		q.turnIn = m_turnInBuf;
		q.prereqs = m_prereqBuffer;
		q.excludes = m_excludesBuffer;
		q.steps = m_stepsBuffer;
		q.rewardXp = m_rewardXpBuffer;
		q.rewardGold = m_rewardGoldBuffer;
		q.rewardItems = m_rewardItemsBuffer;
		q.repeatMode = m_repeatModeBuffer;
		q.cooldownHours = m_cooldownHoursBuffer;
		q.autoComplete = m_autoCompleteBuffer;
		q.title = m_titleBuf;
		q.description = m_descriptionBuf;
		q.stepLabels = m_stepLabelsBuffer;
		q.stepLabels.resize(q.steps.size());
		return q;
	}

	/// Réinitialise tous les buffers d'édition pour saisir une nouvelle quête
	/// (dé-sélectionne, vide id/giver/turnIn/prérequis/**exclusions**/étapes/
	/// récompenses ; **re-réalisation EXT-2 remise à None/0/false**).
	/// Effet de bord : écrase tous les `m_*Buffer`. Main thread (ImGui).
	void QuestEditorPanel::ResetBuffersToNew()
	{
		m_selected = -1;
		m_idBuf[0] = '\0';
		m_giverBuf[0] = '\0';
		m_turnInBuf[0] = '\0';
		m_prereqBuffer.clear();
		m_excludesBuffer.clear();
		m_stepsBuffer.clear();
		m_rewardXpBuffer = 0;
		m_rewardGoldBuffer = 0;
		m_rewardItemsBuffer.clear();
		m_repeatModeBuffer = engine::editor::world::quests::QuestRepeatMode::None;
		m_cooldownHoursBuffer = 0;
		m_autoCompleteBuffer = false;
		m_titleBuf[0] = '\0';
		m_descriptionBuf[0] = '\0';
		m_stepLabelsBuffer.clear();
		m_status.clear();
	}

#if defined(_WIN32)
	void QuestEditorPanel::RenderLoadSection()
	{
		ImGui::TextUnformatted("Charger une quete existante :");
		const char* preview = (m_selected >= 0 && m_selected < static_cast<int>(m_quests.size()))
			? m_quests[m_selected].id.c_str() : "(nouvelle quete)";
		if (ImGui::BeginCombo("Quete a charger", preview))
		{
			for (int i = 0; i < static_cast<int>(m_quests.size()); ++i)
			{
				const bool isSel = (m_selected == i);
				if (ImGui::Selectable(m_quests[i].id.c_str(), isSel))
				{
					m_selected = i;
					LoadBuffersFromSelected();
					m_status = "Quete chargee : " + m_quests[i].id;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("Nouvelle quete")) ResetBuffersToNew();
	}

	void QuestEditorPanel::RenderIdentityFields()
	{
		ImGui::InputText("Id", m_idBuf, sizeof(m_idBuf));
		ImGui::InputText("Donneur (giver)", m_giverBuf, sizeof(m_giverBuf));
		ImGui::InputText("Rendu a (turnIn)", m_turnInBuf, sizeof(m_turnInBuf));
	}

	void QuestEditorPanel::RenderPrereqSection()
	{
		ImGui::TextUnformatted("Prerequis (quetes a completer avant) :");
		if (m_quests.empty())
		{
			ImGui::TextDisabled("(aucune autre quete chargee)");
			return;
		}
		for (const auto& q : m_quests)
		{
			// Une quête ne peut pas être son propre prérequis direct ; les
			// cycles plus longs restent détectés par QuestEditIo::Validate.
			if (q.id == m_idBuf) continue;
			bool checked = false;
			for (const auto& p : m_prereqBuffer) if (p == q.id) { checked = true; break; }
			if (ImGui::Checkbox(q.id.c_str(), &checked))
			{
				if (checked)
				{
					m_prereqBuffer.push_back(q.id);
				}
				else
				{
					m_prereqBuffer.erase(
						std::remove(m_prereqBuffer.begin(), m_prereqBuffer.end(), q.id),
						m_prereqBuffer.end());
				}
			}
		}
	}

	/// Rend la multi-sélection des quêtes mutuellement exclusives (EXT-1) : une
	/// case à cocher par id de quête connu, HORS la quête en cours d'édition
	/// (`q.id == m_idBuf` sauté) pour interdire l'auto-exclusion côté UI.
	/// Cocher ajoute l'id à `m_excludesBuffer`, décocher l'en retire. Miroir de
	/// `RenderPrereqSection` mais sans cycle-check (l'exclusion mutuelle A<->B
	/// est autorisée). Effet de bord : état ImGui + `m_excludesBuffer` (modifié
	/// en place). Thread : main thread (phase ImGui, appelée depuis Render).
	void QuestEditorPanel::RenderExcludesSection()
	{
		ImGui::TextUnformatted("Quetes mutuellement exclusives :");
		if (m_quests.empty())
		{
			ImGui::TextDisabled("(aucune autre quete chargee)");
			return;
		}
		// Namespace ImGui de la section : les cases d'exclusion partagent leur
		// label (`q.id`) avec celles des prérequis ; sans ce PushID de section,
		// les deux ensembles de cases entreraient en collision d'id ImGui.
		ImGui::PushID("excludes_section");
		for (const auto& q : m_quests)
		{
			// Une quête ne peut pas s'exclure elle-même (auto-exclusion interdite,
			// rejetée aussi par QuestEditIo::Validate).
			if (q.id == m_idBuf) continue;
			bool checked = false;
			for (const auto& e : m_excludesBuffer) if (e == q.id) { checked = true; break; }
			if (ImGui::Checkbox(q.id.c_str(), &checked))
			{
				if (checked)
				{
					m_excludesBuffer.push_back(q.id);
				}
				else
				{
					m_excludesBuffer.erase(
						std::remove(m_excludesBuffer.begin(), m_excludesBuffer.end(), q.id),
						m_excludesBuffer.end());
				}
			}
		}
		ImGui::PopID();
	}

	void QuestEditorPanel::RenderStepsSection()
	{
		ImGui::TextUnformatted("Etapes :");
		static const char* kStepTypes[] = { "kill", "collect", "talk", "enter" };
		int removeIdx = -1;
		for (size_t i = 0; i < m_stepsBuffer.size(); ++i)
		{
			EditedStep& step = m_stepsBuffer[i];
			ImGui::PushID(static_cast<int>(i));

			int typeIdx = 0;
			for (int t = 0; t < 4; ++t) if (step.type == kStepTypes[t]) { typeIdx = t; break; }
			if (ImGui::Combo("Type", &typeIdx, kStepTypes, 4)) step.type = kStepTypes[typeIdx];

			char targetBuf[128];
			std::snprintf(targetBuf, sizeof(targetBuf), "%s", step.target.c_str());
			if (ImGui::InputText("Cible", targetBuf, sizeof(targetBuf))) step.target = targetBuf;

			int count = static_cast<int>(step.requiredCount);
			if (ImGui::DragInt("Quantite requise", &count, 1.0f, 1, 100000))
				step.requiredCount = static_cast<uint32_t>(count < 1 ? 1 : count);

			if (ImGui::SmallButton("Suppr etape")) removeIdx = static_cast<int>(i);
			ImGui::Separator();
			ImGui::PopID();
		}
		if (removeIdx >= 0)
		{
			m_stepsBuffer.erase(m_stepsBuffer.begin() + removeIdx);
			if (removeIdx < static_cast<int>(m_stepLabelsBuffer.size()))
				m_stepLabelsBuffer.erase(m_stepLabelsBuffer.begin() + removeIdx);
		}
		if (ImGui::Button("Ajouter une etape"))
		{
			EditedStep s;
			s.type = "kill";
			s.requiredCount = 1;
			m_stepsBuffer.push_back(s);
			m_stepLabelsBuffer.push_back(std::string());
		}
	}

	void QuestEditorPanel::RenderRewardsSection()
	{
		ImGui::TextUnformatted("Recompenses :");
		int xp = static_cast<int>(m_rewardXpBuffer);
		if (ImGui::DragInt("XP", &xp, 1.0f, 0, 1000000)) m_rewardXpBuffer = static_cast<uint32_t>(xp < 0 ? 0 : xp);
		int gold = static_cast<int>(m_rewardGoldBuffer);
		if (ImGui::DragInt("Or", &gold, 1.0f, 0, 1000000)) m_rewardGoldBuffer = static_cast<uint32_t>(gold < 0 ? 0 : gold);

		ImGui::TextUnformatted("Items :");
		int removeIdx = -1;
		for (size_t i = 0; i < m_rewardItemsBuffer.size(); ++i)
		{
			EditedRewardItem& item = m_rewardItemsBuffer[i];
			ImGui::PushID(static_cast<int>(i) + 1000);
			int itemId = static_cast<int>(item.itemId);
			int qty = static_cast<int>(item.quantity);
			if (ImGui::DragInt("Item id", &itemId, 1.0f, 0, 1000000)) item.itemId = static_cast<uint32_t>(itemId < 0 ? 0 : itemId);
			ImGui::SameLine();
			if (ImGui::DragInt("Quantite", &qty, 1.0f, 1, 100000)) item.quantity = static_cast<uint32_t>(qty < 1 ? 1 : qty);
			ImGui::SameLine();
			if (ImGui::SmallButton("Suppr item")) removeIdx = static_cast<int>(i);
			ImGui::PopID();
		}
		if (removeIdx >= 0) m_rewardItemsBuffer.erase(m_rewardItemsBuffer.begin() + removeIdx);
		if (ImGui::Button("Ajouter un item")) m_rewardItemsBuffer.push_back(EditedRewardItem{});
	}

	/// Rend la section EXT-2 « re-réalisation » du formulaire : `Combo` de mode
	/// (5 entrées, indexées dans l'ordre de `QuestRepeatMode`), `DragInt`
	/// « Cooldown (h) » affiché SEULEMENT en mode Cooldown, `Checkbox`
	/// « Auto-complete ». Effet de bord : état ImGui + `m_repeatModeBuffer` /
	/// `m_cooldownHoursBuffer` / `m_autoCompleteBuffer` (modifiés en place).
	/// Thread : main thread (phase ImGui, appelée depuis Render).
	void QuestEditorPanel::RenderRepeatSection()
	{
		using engine::editor::world::quests::QuestRepeatMode;
		ImGui::TextUnformatted("Re-realisation :");
		// Ordre EXACT de QuestRepeatMode (None=0..Cooldown=4) : l'index du combo
		// est directement convertible en enum.
		static const char* kRepeatModes[] = { "Aucun", "Repetable", "Quotidienne", "Hebdo", "Cooldown" };
		int modeIdx = static_cast<int>(m_repeatModeBuffer);
		if (modeIdx < 0 || modeIdx > 4) modeIdx = 0;
		if (ImGui::Combo("Mode", &modeIdx, kRepeatModes, 5))
			m_repeatModeBuffer = static_cast<QuestRepeatMode>(modeIdx);

		// Cooldown (h) : pertinent uniquement en mode Cooldown ; on n'affiche le
		// champ que dans ce cas pour ne pas suggérer qu'il s'applique aux autres.
		if (m_repeatModeBuffer == QuestRepeatMode::Cooldown)
		{
			int hours = static_cast<int>(m_cooldownHoursBuffer);
			if (ImGui::DragInt("Cooldown (h)", &hours, 1.0f, 1, 100000))
				m_cooldownHoursBuffer = static_cast<uint32_t>(hours < 1 ? 1 : hours);
		}

		ImGui::Checkbox("Auto-complete (fin sans retour PNJ)", &m_autoCompleteBuffer);
	}

	void QuestEditorPanel::RenderTextsSection()
	{
		ImGui::TextUnformatted("Textes (fr) :");
		ImGui::InputText("Titre", m_titleBuf, sizeof(m_titleBuf));
		ImGui::InputTextMultiline("Description", m_descriptionBuf, sizeof(m_descriptionBuf), ImVec2(-FLT_MIN, 80.f));
		if (m_stepLabelsBuffer.size() != m_stepsBuffer.size())
			m_stepLabelsBuffer.resize(m_stepsBuffer.size());
		for (size_t i = 0; i < m_stepLabelsBuffer.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i) + 2000);
			char label[160];
			std::snprintf(label, sizeof(label), "Libelle etape %zu", i);
			char buf[160];
			std::snprintf(buf, sizeof(buf), "%s", m_stepLabelsBuffer[i].c_str());
			if (ImGui::InputText(label, buf, sizeof(buf))) m_stepLabelsBuffer[i] = buf;
			ImGui::PopID();
		}
	}

	void QuestEditorPanel::RenderSaveSection()
	{
		if (ImGui::Button("Enregistrer"))
		{
			if (!m_io)
			{
				m_status = "Erreur : QuestEditIo non initialise.";
			}
			else if (m_idBuf[0] == '\0')
			{
				m_status = "Id de quete requis.";
			}
			else
			{
				EditedQuest edited = BuildQuestFromBuffers();
				std::vector<EditedQuest> candidate = m_quests;
				if (m_selected >= 0 && m_selected < static_cast<int>(candidate.size()))
					candidate[m_selected] = edited;
				else
					candidate.push_back(edited);

				std::vector<std::string> errs;
				if (!m_io->Validate(candidate, errs))
				{
					m_status = "Validation echouee :";
					for (const auto& e : errs) m_status += "\n - " + e;
				}
				else
				{
					std::string err;
					if (m_io->Save(m_contentRoot, candidate, err))
					{
						m_quests = candidate;
						// Retrouve l'index de la quête qu'on vient d'éditer (id stable).
						m_selected = -1;
						for (int i = 0; i < static_cast<int>(m_quests.size()); ++i)
							if (m_quests[i].id == edited.id) { m_selected = i; break; }
						m_status = "Quete enregistree : " + edited.id;
					}
					else
					{
						m_status = "Echec sauvegarde : " + err;
					}
				}
			}
		}
	}
#endif

	void QuestEditorPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		EnsureLoaded();
		if (ImGui::Begin("Quest Editor", &m_visible))
		{
			ImGui::TextWrapped(
				"Edite les quetes du contenu actif (quest_definitions.json + "
				"quest_texts.fr.json). Redemarrer le shard pour que le contenu "
				"enregistre soit pris en compte en jeu.");
			ImGui::Separator();

			RenderLoadSection();
			ImGui::Separator();
			RenderIdentityFields();
			ImGui::Separator();
			RenderPrereqSection();
			ImGui::Separator();
			RenderExcludesSection();
			ImGui::Separator();
			RenderStepsSection();
			ImGui::Separator();
			RenderRewardsSection();
			ImGui::Separator();
			RenderRepeatSection();
			ImGui::Separator();
			RenderTextsSection();
			ImGui::Separator();
			RenderSaveSection();

			if (!m_status.empty())
			{
				ImGui::Separator();
				ImGui::TextWrapped("%s", m_status.c_str());
			}
		}
		ImGui::End();
#endif
	}
}
