#include "src/client/render/CharacterWindowImGuiRenderer.h"

#include "src/client/render/SkillBookImGuiRenderer.h"
#include "src/client/render/GrimoireImGuiRenderer.h"
#include "src/client/render/ClassSkillTreeImGuiRenderer.h"
#include "src/client/render/race/RacePreviewViewport.h"
#include "src/client/render/SkillIconCache.h"
#include "src/client/ui_common/UIModel.h"
#include "src/client/ui_common/CurrencyFormat.h"
#include "src/client/inventory/InventoryUi.h"
#include "src/shared/core/Config.h"

#if defined(_WIN32)
#	include "imgui.h"

#include <cstdio>
#include <string>

namespace engine::render
{
	void CharacterWindowImGuiRenderer::Bind(const engine::core::Config* cfg,
		const engine::client::UIModelBinding* uiBinding,
		const engine::client::InventoryUiPresenter* inv,
		engine::client::SkillIconCache* icons,
		engine::render::SkillBookImGuiRenderer* skillBook,
		engine::render::GrimoireImGuiRenderer* grimoire,
		engine::render::ClassSkillTreeImGuiRenderer* classTree)
	{
		m_cfg = cfg;
		m_uiBinding = uiBinding;
		m_inv = inv;
		m_icons = icons;
		m_skillBook = skillBook;
		m_grimoire = grimoire;
		m_classTree = classTree;
	}

	void CharacterWindowImGuiRenderer::Render(const engine::client::UIModel& model)
	{
		if (!m_visible)
			return;

		const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.0f;
		const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.0f;
		// Fenêtre agrandie (retour joueur 2026-07-09 : « trop petite, textes compacts »).
		const float winW = 940.0f;
		const float winH = 640.0f;
		ImGui::SetNextWindowPos(ImVec2((vpW - winW) * 0.5f, (vpH - winH) * 0.5f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.96f);

		if (ImGui::Begin("Personnage##ln_character_window", &m_visible, ImGuiWindowFlags_NoCollapse))
		{
			if (ImGui::BeginTabBar("##ln_character_tabs"))
			{
				if (ImGui::BeginTabItem("Personnage"))
				{
					m_activeTab = Tab::Personnage;
					RenderPersonnageTab(model);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Competences"))
				{
					m_activeTab = Tab::Competences;
					if (m_skillBook)
					{
						m_skillBook->SetEmbedded(true);
						m_skillBook->SetEnabled(true);
						m_skillBook->Render();
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Techniques"))
				{
					m_activeTab = Tab::Techniques;
					if (m_grimoire)
					{
						m_grimoire->SetEmbedded(true);
						m_grimoire->SetEnabled(true);
						m_grimoire->Render();
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Arbre"))
				{
					m_activeTab = Tab::Arbre;
					if (m_classTree)
					{
						m_classTree->SetEmbedded(true);
						m_classTree->SetEnabled(true);
						m_classTree->Render();
					}
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::End();
	}

	void CharacterWindowImGuiRenderer::RenderPersonnageTab(const engine::client::UIModel& model)
	{
		// Deux colonnes : gauche (3D + slots équipement inactifs + caractéristiques),
		// droite (inventaire + argent).
		const float leftW = ImGui::GetContentRegionAvail().x * 0.46f;

		ImGui::BeginChild("##ln_char_left", ImVec2(leftW, 0.0f), false);
		{
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const float previewH = 190.0f;
			if (m_raceViewport != nullptr && m_raceViewport->GetImguiTextureId() != 0u)
			{
				ImGui::Image(static_cast<ImTextureID>(m_raceViewport->GetImguiTextureId()),
					ImVec2(avail.x, previewH));
			}
			else
			{
				// Placeholder tant que le viewport 3D n'est pas branché (Task 4).
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 p0 = ImGui::GetCursorScreenPos();
				dl->AddRectFilled(p0, ImVec2(p0.x + avail.x, p0.y + previewH), IM_COL32(14, 16, 22, 255), 6.0f);
				const char* lbl = "Apercu 3D";
				const ImVec2 ts = ImGui::CalcTextSize(lbl);
				dl->AddText(ImVec2(p0.x + (avail.x - ts.x) * 0.5f, p0.y + previewH * 0.5f - ts.y * 0.5f),
					IM_COL32(120, 130, 160, 255), lbl);
				ImGui::Dummy(ImVec2(avail.x, previewH));
			}

			ImGui::TextDisabled("Equipement — Chantier 2");
			ImGui::Separator();

			// Caractéristiques compactes (ex-fiche F1).
			const engine::client::UIPlayerStats& ps = model.playerStats;
			ImGui::Spacing();
			ImGui::Text("Niveau %u", ps.level);
			// PV : sheetMaxHealth (peuplé par PlayerStats, comme l'ancienne fiche F1) ;
			// maxHealth est piloté par le snapshot et peut rester 0 hors combat.
			ImGui::Text("Points de vie : %u", ps.sheetMaxHealth);
			ImGui::Text("Ressource : %u", ps.secondaryResourceMax);
			ImGui::Text("Degats : %u", ps.damage);
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild("##ln_char_right", ImVec2(0.0f, 0.0f), false);
		{
			ImGui::TextUnformatted("Inventaire");

			if (m_inv != nullptr)
			{
				const engine::client::InventoryPanelState& gi = m_inv->GetState();
				const int cols = (gi.columns > 0u) ? static_cast<int>(gi.columns) : 4;
				const int count = static_cast<int>(gi.slots.size());
				const float cell = 48.0f;
				const float gap = 6.0f;
				ImDrawList* wdl = ImGui::GetWindowDrawList();
				const ImVec2 origin = ImGui::GetCursorScreenPos();
				for (int i = 0; i < count; ++i)
				{
					const engine::client::InventorySlotState& s = gi.slots[static_cast<size_t>(i)];
					const int r = i / cols;
					const int c = i % cols;
					const float x0 = origin.x + static_cast<float>(c) * (cell + gap);
					const float y0 = origin.y + static_cast<float>(r) * (cell + gap);
					const ImVec2 mn(x0, y0);
					const ImVec2 mx(x0 + cell, y0 + cell);
					const bool occ = s.occupied && s.itemId != 0u;
					wdl->AddRectFilled(mn, mx, occ ? IM_COL32(26, 30, 40, 235) : IM_COL32(16, 18, 24, 200), 5.0f);
					wdl->AddRect(mn, mx, occ ? IM_COL32(150, 130, 60, 220) : IM_COL32(64, 66, 74, 180), 5.0f, 0, 1.5f);
					if (!occ)
						continue;
					bool hasIcon = false;
					if (m_icons != nullptr && !s.iconPath.empty())
					{
						const uint64_t tex = m_icons->GetOrLoad(s.iconPath);
						if (tex != 0)
						{
							wdl->AddImage(static_cast<ImTextureID>(tex), ImVec2(x0 + 3, y0 + 3), ImVec2(mx.x - 3, mx.y - 3));
							hasIcon = true;
						}
					}
					if (!hasIcon && !s.label.empty())
						wdl->AddText(ImVec2(x0 + 4, y0 + 5), IM_COL32(220, 220, 225, 255), s.label.substr(0, 7).c_str());
					if (s.quantity > 1u)
					{
						char q[12];
						std::snprintf(q, sizeof(q), "%u", s.quantity);
						const ImVec2 qs = ImGui::CalcTextSize(q);
						wdl->AddText(ImVec2(mx.x - qs.x - 3, mx.y - qs.y - 2), IM_COL32(255, 240, 190, 255), q);
					}
					if (ImGui::IsMouseHoveringRect(mn, mx) && !s.label.empty())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(s.label.c_str());
						ImGui::EndTooltip();
					}
				}
				const int rows = (count + cols - 1) / cols;
				ImGui::Dummy(ImVec2(0.0f, static_cast<float>(rows) * (cell + gap)));
			}

			// Bourse or/argent/bronze (base = bronze, cf. CurrencyFormat.h).
			const engine::client::CoinBreakdown coins = engine::client::SplitCoins(model.wallet.gold);
			ImGui::Separator();
			ImGui::Text("Or %u   Arg %u   Br %u", coins.gold, coins.silver, coins.bronze);
		}
		ImGui::EndChild();
	}
}

#else // !_WIN32 — stub no-op.

namespace engine::render
{
	void CharacterWindowImGuiRenderer::Bind(const engine::core::Config*, const engine::client::UIModelBinding*,
		const engine::client::InventoryUiPresenter*, engine::client::SkillIconCache*,
		engine::render::SkillBookImGuiRenderer*, engine::render::GrimoireImGuiRenderer*,
		engine::render::ClassSkillTreeImGuiRenderer*) {}
	void CharacterWindowImGuiRenderer::Render(const engine::client::UIModel&) {}
	void CharacterWindowImGuiRenderer::RenderPersonnageTab(const engine::client::UIModel&) {}
}

#endif
