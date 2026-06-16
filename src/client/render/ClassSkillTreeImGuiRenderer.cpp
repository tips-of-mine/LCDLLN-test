#include "src/client/render/ClassSkillTreeImGuiRenderer.h"
#include "src/client/skills/ClassSkillTreeUi.h"
#include "src/client/render/SkillIconCache.h"

#if defined(_WIN32)
#	include "imgui.h"

#include <string>

namespace engine::render
{
	void ClassSkillTreeImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || !m_enabled || !m_presenter->IsInitialized())
		{
			return;
		}
		const auto& state = m_presenter->GetState();

		const float panelW = 860.f;
		const float panelH = 620.f;
		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
		ImGui::SetNextWindowPos(ImVec2((vpW - panelW) * 0.5f, (vpH - panelH) * 0.5f),
		                        ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.96f);

		const std::string windowTitle = "Arbre de competences — "
		    + (state.classId.empty() ? std::string("(aucune classe)") : state.classId)
		    + "##ln_skilltree";

		if (ImGui::Begin(windowTitle.c_str(), nullptr, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::TextDisabled("Classe : %s   |   Niveau joueur : %u",
			    state.classId.empty() ? "(inconnue)" : state.classId.c_str(),
			    state.playerLevel);
			ImGui::Separator();

			// 3 colonnes : une par branche (single / aoe / def).
			ImGui::Columns(3, "##ln_skilltree_cols", true);

			// Noms lisibles des branches.
			static const char* kBranchLabels[] = { "Branche : Single", "Branche : AoE", "Branche : Def" };
			static const char* kChildIds[]     = { "##skt_col_single", "##skt_col_aoe", "##skt_col_def" };

			for (int colIdx = 0; colIdx < 3; ++colIdx)
			{
				ImGui::TextUnformatted(kBranchLabels[colIdx]);
				ImGui::Separator();

				// Cherche la branche correspondante dans l'état.
				const engine::client::SkillTreeBranch* branch = nullptr;
				if (colIdx < static_cast<int>(state.branches.size()))
				{
					branch = &state.branches[static_cast<size_t>(colIdx)];
				}

				// Zone défilante pour les 60 paliers.
				ImGui::BeginChild(kChildIds[colIdx], ImVec2(0, 0), true);

				if (branch == nullptr || branch->tiers.empty())
				{
					ImGui::TextDisabled("(aucun skill)");
				}
				else
				{
					for (const engine::client::SkillTreeCell& cell : branch->tiers)
					{
						ImGui::PushID(cell.skill.skillId.c_str());

						// Icône du palier (si disponible) à gauche du libellé. Repli
						// silencieux sur le texte seul si pas de cache / pas d'icône.
						if (m_iconCache != nullptr && !cell.skill.iconFile.empty() && !state.classId.empty())
						{
							const std::string iconPath =
								"icons/skills/" + state.classId + "/" + cell.skill.iconFile;
							const uint64_t texId = m_iconCache->GetOrLoad(iconPath);
							if (texId != 0)
							{
								ImGui::Image(static_cast<ImTextureID>(texId), ImVec2(28.0f, 28.0f));
								ImGui::SameLine();
							}
						}

						// En-tête de palier : numéro de tier + nom du skill.
						ImGui::Text("Tier %u — %s", cell.skill.tier, cell.skill.name.c_str());

						// Tags compacts (coût / CD / effet).
						const std::string tags =
						    std::string("coût ") + std::to_string(cell.skill.resourceCostPercent) + "%"
						    + "  CD " + std::to_string(cell.skill.cooldownMs / 1000u) + "s"
						    + "  " + cell.skill.effectKind;
						ImGui::TextDisabled("%s", tags.c_str());

						// Affichage selon statut.
						switch (cell.status)
						{
							case engine::client::SkillCellStatus::Chosen:
								ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.f), "  Appris");
								break;

							case engine::client::SkillCellStatus::Available:
							{
								const std::string btnLabel =
								    std::string("Choisir##") + branch->id
								    + "_" + std::to_string(cell.skill.tier);
								if (ImGui::Button(btnLabel.c_str()))
								{
									m_presenter->ChooseSkill(cell.skill.level, cell.skill.skillId);
								}
								break;
							}

							case engine::client::SkillCellStatus::Locked:
								ImGui::TextDisabled("  (niveau %u)", cell.skill.tier);
								break;
						}

						ImGui::Separator();
						ImGui::PopID();
					}
				}

				ImGui::EndChild();

				if (colIdx < 2)
				{
					ImGui::NextColumn();
				}
			}

			ImGui::Columns(1);
		}
		ImGui::End();
	}
}

#else  // !_WIN32 — stub no-op (pas d'ImGui hors client Windows).

namespace engine::render
{
	void ClassSkillTreeImGuiRenderer::Render() {}
}

#endif
