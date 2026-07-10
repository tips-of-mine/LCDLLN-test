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
#include "src/shared/items/ItemCatalog.h"

#if defined(_WIN32)
#	include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace engine::render
{
	namespace
	{
		// Chantier 2 SP-A — libellé français d'un slot d'équipement (l'enum ToString
		// renvoie des tokens anglais réservés au wire/JSON).
		const char* SlotLabelFr(engine::items::EquipmentSlot slot)
		{
			switch (slot)
			{
			case engine::items::EquipmentSlot::Head:     return "Tete";
			case engine::items::EquipmentSlot::Chest:    return "Torse";
			case engine::items::EquipmentSlot::Legs:     return "Jambes";
			case engine::items::EquipmentSlot::Feet:     return "Pieds";
			case engine::items::EquipmentSlot::Hands:    return "Mains";
			case engine::items::EquipmentSlot::MainHand: return "Main droite";
			case engine::items::EquipmentSlot::OffHand:  return "Main gauche";
			case engine::items::EquipmentSlot::Amulet:   return "Amulette";
			case engine::items::EquipmentSlot::Ring1:    return "Anneau 1";
			case engine::items::EquipmentSlot::Ring2:    return "Anneau 2";
			default:                                     return "?";
			}
		}
	}

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
		// Fenêtre agrandie (retours joueur 2026-07-09) : plus haute pour l'aperçu 3D
		// et assez large pour un inventaire 8x8.
		const float winW = 1120.0f;
		const float winH = 740.0f;
		ImGui::SetNextWindowPos(ImVec2((vpW - winW) * 0.5f, (vpH - winH) * 0.5f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.96f);

		if (ImGui::Begin("Personnage##ln_character_window", &m_visible, ImGuiWindowFlags_NoCollapse))
		{
			// Sélection d'onglet programmatique (OpenAtTab via slash commands / menu).
			auto tabFlag = [&](Tab t) -> ImGuiTabItemFlags {
				return (m_hasPendingTab && m_pendingTab == t) ? ImGuiTabItemFlags_SetSelected : 0;
			};
			if (ImGui::BeginTabBar("##ln_character_tabs"))
			{
				if (ImGui::BeginTabItem("Personnage", nullptr, tabFlag(Tab::Personnage)))
				{
					m_activeTab = Tab::Personnage;
					RenderPersonnageTab(model);
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Competences", nullptr, tabFlag(Tab::Competences)))
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
				if (ImGui::BeginTabItem("Techniques", nullptr, tabFlag(Tab::Techniques)))
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
				if (ImGui::BeginTabItem("Arbre", nullptr, tabFlag(Tab::Arbre)))
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
			m_hasPendingTab = false; // sélection consommée pour cette frame.
		}
		ImGui::End();
	}

	void CharacterWindowImGuiRenderer::RenderPersonnageTab(const engine::client::UIModel& model)
	{
		// Deux colonnes : gauche (3D + slots équipement inactifs + caractéristiques),
		// droite (inventaire + argent). Tout est dimensionné DYNAMIQUEMENT à partir de
		// l'espace disponible pour ne laisser aucun vide (retour joueur 2026-07-09 :
		// grille/aperçu qui ne remplissaient pas la fenêtre).
		const float leftW = ImGui::GetContentRegionAvail().x * 0.38f;

		ImGui::BeginChild("##ln_char_left", ImVec2(leftW, 0.0f), false);
		{
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			// Hauteur réservée au bloc caractéristiques (titre + séparateur + 4 lignes).
			const float lineH = ImGui::GetTextLineHeightWithSpacing();
			const float statsH = lineH * 6.0f + 16.0f;
			// Chantier 2 SP-A — hauteur réservée au panneau équipement (titre + séparateur
			// + 5 rangées de 2 slots).
			const float equipH = lineH * 7.0f + 8.0f;
			// Aperçu CARRÉ (texture 512x512) agrandi pour remplir la colonne : borné par
			// la largeur ET par la hauteur restante une fois équipement + stats retirés.
			float side = std::min(avail.x, avail.y - statsH - equipH);
			side = std::max(side, 140.0f);
			const float offX = (avail.x - side) * 0.5f;
			// Centrage vertical du bloc (aperçu + stats) : répartit l'espace résiduel
			// en haut/bas au lieu de le laisser béant sous les stats.
			const float blockH = side + equipH + statsH;
			const float padTop = std::max(0.0f, (avail.y - blockH) * 0.5f);
			if (padTop > 0.0f)
				ImGui::Dummy(ImVec2(1.0f, padTop));

			if (m_raceViewport != nullptr && m_raceViewport->GetImguiTextureId() != 0u)
			{
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offX);
				ImGui::Image(static_cast<ImTextureID>(m_raceViewport->GetImguiTextureId()),
					ImVec2(side, side));
				// Rotation manuelle : glisser (bouton gauche) sur l'aperçu tourne le
				// perso ; au repos il reste immobile, face caméra. Auto-orbit coupée.
				m_raceViewport->SetAutoOrbit(false);
				if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
					m_previewYaw += ImGui::GetIO().MouseDelta.x * 0.012f;
				m_raceViewport->SetOrbitYaw(m_previewYaw);
			}
			else
			{
				// Placeholder tant que le viewport 3D n'est pas branché.
				ImDrawList* dl = ImGui::GetWindowDrawList();
				const ImVec2 p0raw = ImGui::GetCursorScreenPos();
				const ImVec2 p0(p0raw.x + offX, p0raw.y);
				dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(14, 16, 22, 255), 6.0f);
				const char* lbl = "Apercu 3D";
				const ImVec2 ts = ImGui::CalcTextSize(lbl);
				dl->AddText(ImVec2(p0.x + (side - ts.x) * 0.5f, p0.y + side * 0.5f - ts.y * 0.5f),
					IM_COL32(120, 130, 160, 255), lbl);
				ImGui::Dummy(ImVec2(avail.x, side));
			}

			ImGui::Spacing();
			ImGui::TextDisabled("Equipement");
			ImGui::Separator();

			// Chantier 2 SP-A — grille des 10 slots (2 colonnes × 5 rangées). Slot occupé
			// => nom de l'objet porté + clic pour déséquiper (renvoi au sac). Le serveur
			// est autoritaire : le clic n'émet qu'une intention, drainée par Engine.
			{
				// Table slot(1..10) -> itemId porté, depuis le snapshot EquipmentUpdate.
				uint32_t worn[engine::items::kEquipSlotCount + 1] = {};
				for (const engine::server::EquipmentEntry& e : model.equipment)
				{
					if (e.slot >= 1u && e.slot <= engine::items::kEquipSlotCount)
						worn[e.slot] = e.itemId;
				}
				const float colW = ImGui::GetContentRegionAvail().x * 0.5f - 2.0f;
				for (std::size_t slot = 1; slot <= engine::items::kEquipSlotCount; ++slot)
				{
					const engine::items::EquipmentSlot es = static_cast<engine::items::EquipmentSlot>(slot);
					const uint32_t itemId = worn[slot];
					std::string itemName;
					bool occupied = false;
					if (itemId != 0u)
					{
						occupied = true;
						if (m_itemCatalog != nullptr)
						{
							if (const engine::items::ItemDefinition* def = m_itemCatalog->Find(itemId))
								itemName = def->name;
						}
						if (itemName.empty())
							itemName = "#" + std::to_string(itemId);
					}
					char label[96];
					std::snprintf(label, sizeof(label), "%s: %s##eqslot%zu",
						SlotLabelFr(es), occupied ? itemName.c_str() : "-", slot);
					ImGui::Selectable(label, false, 0, ImVec2(colW, 0.0f));
					if (occupied && ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(itemName.c_str());
						ImGui::TextDisabled("Emplacement : %s", SlotLabelFr(es));
						ImGui::Separator();
						ImGui::TextDisabled("Clic : déséquiper");
						ImGui::EndTooltip();
					}
					if (occupied && ImGui::IsItemClicked(ImGuiMouseButton_Left))
					{
						m_pendingEquip = PendingEquipAction{
							PendingEquipAction::Kind::Unequip, 0u, static_cast<uint8_t>(slot)};
					}
					// 2 colonnes : slot impair à gauche, pair sur la même ligne à droite.
					if ((slot % 2u) == 1u && slot != engine::items::kEquipSlotCount)
						ImGui::SameLine(0.0f, 4.0f);
				}
			}

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
			ImGui::Spacing();

			if (m_inv != nullptr)
			{
				const engine::client::InventoryPanelState& gi = m_inv->GetState();
				const int cols = (gi.columns > 0u) ? static_cast<int>(gi.columns) : 4;
				const int count = static_cast<int>(gi.slots.size());
				const int rows = (count + cols - 1) / cols;
				const float gap = 7.0f;
				// Espace restant sous le titre, en réservant une ligne pour la bourse.
				const ImVec2 region = ImGui::GetContentRegionAvail();
				const float moneyH = ImGui::GetFrameHeightWithSpacing()
					+ ImGui::GetTextLineHeightWithSpacing();
				const float gridAvailW = region.x;
				const float gridAvailH = std::max(0.0f, region.y - moneyH);
				// Cellule CARRÉE dimensionnée pour remplir au mieux la zone (largeur OU
				// hauteur, le plus contraignant) → les cases grossissent, plus de vide.
				const float cellW = (cols > 0) ? (gridAvailW - static_cast<float>(cols - 1) * gap)
					/ static_cast<float>(cols) : 0.0f;
				const float cellH = (rows > 0) ? (gridAvailH - static_cast<float>(rows - 1) * gap)
					/ static_cast<float>(rows) : 0.0f;
				float cell = std::max(24.0f, std::min(cellW, cellH));
				const float gridW = static_cast<float>(cols) * cell + static_cast<float>(cols - 1) * gap;
				const float gridH = static_cast<float>(rows) * cell + static_cast<float>(rows - 1) * gap;
				// Centrage de la grille dans la zone disponible (marges équilibrées).
				const float offX = std::max(0.0f, (gridAvailW - gridW) * 0.5f);
				const float offY = std::max(0.0f, (gridAvailH - gridH) * 0.5f);
				ImDrawList* wdl = ImGui::GetWindowDrawList();
				const ImVec2 cur = ImGui::GetCursorScreenPos();
				const ImVec2 origin(cur.x + offX, cur.y + offY);
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
					// Chantier 2 SP-A — objet équipable ? (catalogue client, source d'affichage).
					const engine::items::ItemDefinition* def =
						(m_itemCatalog != nullptr) ? m_itemCatalog->Find(s.itemId) : nullptr;
					const bool equippable = (def != nullptr) && def->IsEquippable();
					if (ImGui::IsMouseHoveringRect(mn, mx) && !s.label.empty())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(s.label.c_str());
						if (def != nullptr)
						{
							if (equippable)
								ImGui::TextDisabled("Emplacement : %s", SlotLabelFr(def->slot));
							const engine::items::StatBonus& b = def->bonus;
							if (b.hp)          ImGui::Text("PV +%d", b.hp);
							if (b.resource)    ImGui::Text("Ressource +%d", b.resource);
							if (b.damage)      ImGui::Text("Degats +%d", b.damage);
							if (b.accuracy)    ImGui::Text("Precision +%d", b.accuracy);
							if (b.range != 0.0f)       ImGui::Text("Portee +%.1f m", b.range);
							if (b.critRate != 0.0f)    ImGui::Text("Crit +%.1f%%", b.critRate);
							if (b.critMult != 0.0f)    ImGui::Text("Mult. crit +%.2f", b.critMult);
							if (b.speedWalk != 0.0f)   ImGui::Text("Vitesse marche +%.2f", b.speedWalk);
							if (b.speedRun != 0.0f)    ImGui::Text("Vitesse course +%.2f", b.speedRun);
							if (b.speedSprint != 0.0f) ImGui::Text("Vitesse sprint +%.2f", b.speedSprint);
							if (b.stamina)     ImGui::Text("Endurance +%d", b.stamina);
							if (b.perception)  ImGui::Text("Perception +%d", b.perception);
							if (b.stealth)     ImGui::Text("Discretion +%d", b.stealth);
							if (equippable)
							{
								ImGui::Separator();
								ImGui::TextDisabled("Clic : equiper");
							}
						}
						ImGui::EndTooltip();
					}
					// Clic gauche sur un objet équipable => intention d'équipement (drainée
					// par Engine). Le serveur revalide (possession + slot).
					if (equippable && ImGui::IsMouseHoveringRect(mn, mx)
						&& ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						m_pendingEquip = PendingEquipAction{
							PendingEquipAction::Kind::Equip, s.itemId, 0u};
					}
				}
				// Réserve toute la zone grille pour pousser la bourse en bas.
				ImGui::Dummy(ImVec2(gridAvailW, gridAvailH));
			}

			// Bourse or/argent/bronze (base = bronze, cf. CurrencyFormat.h), épinglée en bas.
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
