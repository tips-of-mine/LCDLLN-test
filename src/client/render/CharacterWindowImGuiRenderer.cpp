#include "src/client/render/CharacterWindowImGuiRenderer.h"

#include "src/client/render/SkillBookImGuiRenderer.h"
#include "src/client/render/GrimoireImGuiRenderer.h"
#include "src/client/render/ClassSkillTreeImGuiRenderer.h"
#include "src/client/render/ExploitsImGuiRenderer.h" // SP2 anniversaires (2026-07-18)
#include "src/shared/anniversary/CakeItemToken.h"    // SP3 anniversaires (2026-07-18)
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

		// Chantier 2 SP-A — lignes de bonus d'un objet dans une infobulle (n'affiche
		// que les champs non nuls). Réutilisé par les cellules d'équipement et
		// l'inventaire. À appeler entre BeginTooltip/EndTooltip.
		void AppendBonusTooltipLines(const engine::items::StatBonus& b)
		{
			if (b.hp)                  ImGui::Text("PV +%d", b.hp);
			if (b.resource)            ImGui::Text("Ressource +%d", b.resource);
			if (b.damage)              ImGui::Text("Degats +%d", b.damage);
			if (b.accuracy)            ImGui::Text("Precision +%d", b.accuracy);
			if (b.range != 0.0f)       ImGui::Text("Portee +%.1f m", b.range);
			if (b.critRate != 0.0f)    ImGui::Text("Crit +%.1f%%", b.critRate);
			if (b.critMult != 0.0f)    ImGui::Text("Mult. crit +%.2f", b.critMult);
			if (b.speedWalk != 0.0f)   ImGui::Text("Vitesse marche +%.2f", b.speedWalk);
			if (b.speedRun != 0.0f)    ImGui::Text("Vitesse course +%.2f", b.speedRun);
			if (b.speedSprint != 0.0f) ImGui::Text("Vitesse sprint +%.2f", b.speedSprint);
			if (b.stamina)             ImGui::Text("Endurance +%d", b.stamina);
			if (b.perception)          ImGui::Text("Perception +%d", b.perception);
			if (b.stealth)             ImGui::Text("Discretion +%d", b.stealth);
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
				// Onglet renommé « Equipement » pour ne pas dupliquer le titre de la
				// fenêtre (« Personnage »), source de confusion (retour joueur 2026-07-11).
				if (ImGui::BeginTabItem("Equipement", nullptr, tabFlag(Tab::Personnage)))
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
				// SP2 anniversaires (2026-07-18) — onglet absent si le renderer
				// n'est pas câblé (rétro-compatible).
				if (m_exploits != nullptr
					&& ImGui::BeginTabItem("Exploits", nullptr, tabFlag(Tab::Exploits)))
				{
					m_activeTab = Tab::Exploits;
					m_exploits->SetEmbedded(true);
					m_exploits->Render();
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
			const float gap = 6.0f;

			// Chantier 2 SP-A — « paperdoll » : deux colonnes de 5 cellules d'équipement
			// encadrent l'aperçu 3D (5 à gauche, 5 à droite). L'aperçu est RÉDUIT en
			// largeur (reste carré, non déformé) pour dégager la place des cellules
			// (retour joueur 2026-07-11). Bande = cell + gap + S + gap + cell ; les
			// cellules s'alignent sur la hauteur S : cell = (S-4·gap)/5, d'où pour une
			// largeur de bande W : S = (5·W - 2·gap)/7.
			uint32_t worn[engine::items::kEquipSlotCount + 1] = {};
			for (const engine::server::EquipmentEntry& e : model.equipment)
			{
				if (e.slot >= 1u && e.slot <= engine::items::kEquipSlotCount)
					worn[e.slot] = e.itemId;
			}
			float side = (5.0f * avail.x - 2.0f * gap) / 7.0f;
			// Borné par la hauteur disponible : bande (side) + rangée Waist
			// (cell + gap, cf. cellule Waist dessinée sous la colonne gauche) +
			// rangée « Ceinture » (contenu, ~58 px libellé compris — retour
			// joueur 2026-07-21) + stats. Avec cell = (side - 4·gap)/5, la
			// contrainte side + gap + cell + 58 + statsH ≤ avail.y se résout en :
			side = std::min(side, (5.0f * (avail.y - statsH - 58.0f - gap) + 4.0f * gap) / 6.0f);
			side = std::max(side, 150.0f);
			const float cell = std::max(30.0f, (side - 4.0f * gap) / 5.0f);
			const float bandW = 2.0f * cell + 2.0f * gap + side;
			const float offX = std::max(0.0f, (avail.x - bandW) * 0.5f);
			// Retour joueur 2026-07-20 — bloc ANCRÉ EN HAUT (plus de centrage
			// vertical) : le centrage laissait un grand vide au-dessus du
			// paperdoll, impression que l'aperçu du personnage était descendu.

			// Repères écran de la bande (dessin manuel via draw list).
			const ImVec2 bandOrigin = ImGui::GetCursorScreenPos();
			const float bandX = bandOrigin.x + offX;
			const float bandY = bandOrigin.y;
			const float previewX = bandX + cell + gap;
			const float rightX = previewX + side + gap;
			ImDrawList* wdl = ImGui::GetWindowDrawList();

			// Rendu d'une cellule d'équipement (dessin manuel, comme l'inventaire).
			// Slot vide => libellé grisé du slot (repère « ce qui va là ») ; occupé =>
			// icône de l'objet (ou libellé). Survol = infobulle ; clic = déséquiper.
			auto drawEquipCell = [&](std::size_t slot, float x0, float y0)
			{
				const engine::items::EquipmentSlot es = static_cast<engine::items::EquipmentSlot>(slot);
				const uint32_t itemId = worn[slot];
				const bool occ = itemId != 0u;
				const ImVec2 mn(x0, y0);
				const ImVec2 mx(x0 + cell, y0 + cell);
				wdl->AddRectFilled(mn, mx, occ ? IM_COL32(26, 30, 40, 235) : IM_COL32(16, 18, 24, 200), 5.0f);
				wdl->AddRect(mn, mx, occ ? IM_COL32(150, 130, 60, 220) : IM_COL32(64, 66, 74, 180), 5.0f, 0, 1.5f);
				const engine::items::ItemDefinition* def =
					(occ && m_itemCatalog != nullptr) ? m_itemCatalog->Find(itemId) : nullptr;
				bool hasIcon = false;
				if (def != nullptr && m_icons != nullptr && !def->iconPath.empty())
				{
					const uint64_t tex = m_icons->GetOrLoad(def->iconPath);
					if (tex != 0)
					{
						wdl->AddImage(static_cast<ImTextureID>(tex), ImVec2(x0 + 3, y0 + 3), ImVec2(mx.x - 3, mx.y - 3));
						hasIcon = true;
					}
				}
				if (!hasIcon)
				{
					const std::string txt = std::string(SlotLabelFr(es)).substr(0, 6);
					const ImU32 col = occ ? IM_COL32(220, 220, 225, 255) : IM_COL32(120, 124, 134, 255);
					wdl->AddText(ImVec2(x0 + 4, y0 + cell * 0.5f - 7.0f), col, txt.c_str());
				}
				// Item ImGui superposé (les visuels sont déjà dessinés via wdl) : porte
				// le survol, le clic-déséquiper ET la cible de glisser-déposer.
				ImGui::SetCursorScreenPos(mn);
				char cellId[24];
				std::snprintf(cellId, sizeof(cellId), "##eqcell%zu", slot);
				ImGui::InvisibleButton(cellId, ImVec2(cell, cell));
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextDisabled("%s", SlotLabelFr(es));
					if (occ)
					{
						const std::string itemName = (def != nullptr && !def->name.empty())
							? def->name : ("#" + std::to_string(itemId));
						ImGui::TextUnformatted(itemName.c_str());
						if (def != nullptr)
							AppendBonusTooltipLines(def->bonus);
						ImGui::Separator();
						ImGui::TextDisabled("Clic / relâcher ici : déséquiper / équiper");
					}
					else
					{
						ImGui::TextDisabled("(vide) — glissez un objet ici");
					}
					ImGui::EndTooltip();
				}
				// Clic simple (relâché sur la cellule, hors glisser) => déséquiper.
				if (occ && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
					&& ImGui::GetDragDropPayload() == nullptr)
				{
					m_pendingEquip = PendingEquipAction{
						PendingEquipAction::Kind::Unequip, 0u, static_cast<uint8_t>(slot)};
				}
				// Glisser-déposer : accepte un objet équipable lâché depuis l'inventaire.
				// Le serveur reste autoritaire (choisit le slot réel + valide la possession),
				// on émet donc simplement l'intention d'équipement de l'itemId lâché.
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("LN_EQUIP_ITEM"))
					{
						if (pl->DataSize == static_cast<int>(sizeof(uint32_t)))
						{
							const uint32_t dropped = *static_cast<const uint32_t*>(pl->Data);
							m_pendingEquip = PendingEquipAction{
								PendingEquipAction::Kind::Equip, dropped, 0u};
						}
					}
					ImGui::EndDragDropTarget();
				}
			};

			// Colonne gauche (slots 1..5 : tête, torse, jambes, pieds, mains).
			for (std::size_t i = 0; i < 5; ++i)
				drawEquipCell(1 + i, bandX, bandY + static_cast<float>(i) * (cell + gap));
			// Colonne droite (slots 6..10 : arme, bouclier, amulette, 2 anneaux).
			for (std::size_t i = 0; i < 5; ++i)
				drawEquipCell(6 + i, rightX, bandY + static_cast<float>(i) * (cell + gap));
			// Ceinture v2 (2026-07-20) — 11e cellule (slot Waist) en bas de la
			// colonne gauche : la ceinture équipée porte la capacité de la
			// barre d'objets actifs (4 par défaut, 12 max).
			drawEquipCell(static_cast<std::size_t>(engine::items::EquipmentSlot::Waist),
				bandX, bandY + 5.0f * (cell + gap));

			// Aperçu 3D au centre (carré, non déformé), réduit en largeur pour laisser
			// la place aux deux colonnes de cellules.
			if (m_raceViewport != nullptr && m_raceViewport->GetImguiTextureId() != 0u)
			{
				ImGui::SetCursorScreenPos(ImVec2(previewX, bandY));
				ImGui::Image(static_cast<ImTextureID>(m_raceViewport->GetImguiTextureId()),
					ImVec2(side, side));
				// Rotation manuelle : glisser (bouton gauche) sur l'aperçu tourne le perso.
				m_raceViewport->SetAutoOrbit(false);
				if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
					m_previewYaw += ImGui::GetIO().MouseDelta.x * 0.012f;
				m_raceViewport->SetOrbitYaw(m_previewYaw);
			}
			else
			{
				// Placeholder tant que le viewport 3D n'est pas branché.
				wdl->AddRectFilled(ImVec2(previewX, bandY), ImVec2(previewX + side, bandY + side),
					IM_COL32(14, 16, 22, 255), 6.0f);
				const char* lbl = "Apercu 3D";
				const ImVec2 ts = ImGui::CalcTextSize(lbl);
				wdl->AddText(ImVec2(previewX + (side - ts.x) * 0.5f, bandY + side * 0.5f - ts.y * 0.5f),
					IM_COL32(120, 130, 160, 255), lbl);
			}

			// Retour joueur 2026-07-21 — la ceinture a son EMPLACEMENT DÉDIÉ dans
			// la fiche du personnage (références WoW / Diablo IV / Dune : le
			// contenu de la ceinture visible dans la fenêtre, pas seulement sur
			// le HUD). Rangée « Ceinture » sous le paperdoll : les N cases du
			// contenu (icône ou initiales, quantité restante en sac, tooltips),
			// CIBLES de glisser-déposer depuis le sac (payload LN_EQUIP_ITEM) et
			// réorganisables entre elles (LN_BELT_MOVE — payload PARTAGÉ avec la
			// barre HUD : on peut aussi glisser de la fenêtre vers le HUD et
			// inversement). Clic droit = retirer. L'ACTIVATION reste sur la
			// barre HUD (Maj+1..9 / clic) : ici on organise, on ne consomme pas.
			const std::vector<std::string>& belt = model.playerStats.beltLayout;
			const float beltRowY = bandY + 5.0f * (cell + gap) + cell + 8.0f;
			float statsY = beltRowY;
			if (!belt.empty())
			{
				const size_t beltN = belt.size();
				// Cases dimensionnées pour tenir toute la bande (12 max), sans
				// dépasser la taille des cellules d'équipement.
				const float beltCell = std::min(cell,
					(bandW - static_cast<float>(beltN - 1) * gap) / static_cast<float>(beltN));
				wdl->AddText(ImVec2(bandX, beltRowY), IM_COL32(150, 180, 155, 230), "Ceinture");
				char capTxt[24];
				std::snprintf(capTxt, sizeof(capTxt), "%d emplacement(s)", static_cast<int>(beltN));
				const ImVec2 capSz = ImGui::CalcTextSize(capTxt);
				wdl->AddText(ImVec2(bandX + bandW - capSz.x, beltRowY),
					IM_COL32(120, 130, 140, 200), capTxt);
				const float beltY0 = beltRowY + 18.0f;
				std::vector<std::string> newBelt(belt.begin(), belt.end());
				bool beltEdited = false;
				for (size_t bi = 0; bi < beltN; ++bi)
				{
					const float bx0 = bandX + static_cast<float>(bi) * (beltCell + gap);
					uint32_t beltItemId = 0u;
					const bool bOcc = engine::anniversary::ParseItemToken(belt[bi], beltItemId);
					const engine::items::ItemDefinition* bdef =
						(bOcc && m_itemCatalog != nullptr) ? m_itemCatalog->Find(beltItemId) : nullptr;
					ImGui::SetCursorScreenPos(ImVec2(bx0, beltY0));
					char beltId[24];
					std::snprintf(beltId, sizeof(beltId), "##fbelt%zu", bi);
					ImGui::InvisibleButton(beltId, ImVec2(beltCell, beltCell));
					const bool bHov = ImGui::IsItemHovered();
					// Quantité restante en sac (somme des piles) — même jauge que le HUD.
					uint32_t bQty = 0u;
					if (bOcc && m_inv != nullptr)
					{
						for (const engine::client::InventorySlotState& is : m_inv->GetState().slots)
							if (is.occupied && is.itemId == beltItemId)
								bQty += is.quantity;
					}
					// Visuel : mêmes codes que la barre HUD (fond vert sombre,
					// bordure dorée au survol) pour que le lien saute aux yeux.
					wdl->AddRectFilled(ImVec2(bx0, beltY0),
						ImVec2(bx0 + beltCell, beltY0 + beltCell),
						bOcc ? IM_COL32(32, 46, 36, 235) : IM_COL32(14, 16, 22, 170), 6.0f);
					wdl->AddRect(ImVec2(bx0, beltY0),
						ImVec2(bx0 + beltCell, beltY0 + beltCell),
						bHov ? IM_COL32(235, 205, 120, 255)
						     : IM_COL32(120, 170, 130, bOcc ? 220 : 110),
						6.0f, 0, bHov ? 2.5f : 2.0f);
					if (bOcc)
					{
						bool bIcon = false;
						if (bdef != nullptr && m_icons != nullptr && !bdef->iconPath.empty())
						{
							const uint64_t tex = m_icons->GetOrLoad(bdef->iconPath);
							if (tex != 0)
							{
								wdl->AddImage(static_cast<ImTextureID>(tex),
									ImVec2(bx0 + 3.0f, beltY0 + 3.0f),
									ImVec2(bx0 + beltCell - 3.0f, beltY0 + beltCell - 3.0f));
								bIcon = true;
							}
						}
						if (!bIcon)
						{
							const char* bname = (bdef != nullptr && !bdef->name.empty())
								? bdef->name.c_str() : "?";
							char initials[3] = { bname[0], bname[1] != '\0' ? bname[1] : ' ', '\0' };
							const ImVec2 initSz = ImGui::CalcTextSize(initials);
							wdl->AddText(ImVec2(bx0 + (beltCell - initSz.x) * 0.5f,
								beltY0 + (beltCell - initSz.y) * 0.5f),
								IM_COL32(240, 250, 240, 255), initials);
						}
						char bQtyTxt[12];
						std::snprintf(bQtyTxt, sizeof(bQtyTxt), "%u", bQty);
						const ImVec2 qtySz = ImGui::CalcTextSize(bQtyTxt);
						wdl->AddText(ImVec2(bx0 + beltCell - qtySz.x - 3.0f,
							beltY0 + beltCell - qtySz.y - 2.0f),
							bQty > 0u ? IM_COL32(255, 240, 190, 255)
							          : IM_COL32(255, 120, 110, 255),
							bQtyTxt);
					}
					// Source de drag (réorganisation — payload partagé avec le HUD).
					if (bOcc && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
					{
						int fromIndex = static_cast<int>(bi);
						ImGui::SetDragDropPayload("LN_BELT_MOVE", &fromIndex, sizeof(int));
						ImGui::TextUnformatted((bdef != nullptr && !bdef->name.empty())
							? bdef->name.c_str() : "Objet");
						ImGui::EndDragDropSource();
					}
					// Cible de drop : échange interne OU consommable lâché du sac.
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* mv = ImGui::AcceptDragDropPayload("LN_BELT_MOVE"))
						{
							if (mv->DataSize == static_cast<int>(sizeof(int)))
							{
								const int from = *static_cast<const int*>(mv->Data);
								if (from >= 0 && from < static_cast<int>(newBelt.size())
									&& from != static_cast<int>(bi))
								{
									std::swap(newBelt[static_cast<size_t>(from)], newBelt[bi]);
									beltEdited = true;
								}
							}
						}
						if (const ImGuiPayload* it = ImGui::AcceptDragDropPayload("LN_EQUIP_ITEM"))
						{
							if (it->DataSize == static_cast<int>(sizeof(uint32_t)))
							{
								const uint32_t droppedId = *static_cast<const uint32_t*>(it->Data);
								const std::string droppedTok = engine::anniversary::MakeItemToken(droppedId);
								bool already = false;
								for (const std::string& s : newBelt)
									if (s == droppedTok) { already = true; break; }
								if (!already)
								{
									newBelt[bi] = droppedTok;
									beltEdited = true;
								}
							}
						}
						ImGui::EndDragDropTarget();
					}
					// Clic droit : vider la case.
					if (bOcc && bHov && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						newBelt[bi].clear();
						beltEdited = true;
					}
					// Tooltip.
					if (bHov)
					{
						ImGui::BeginTooltip();
						if (bOcc)
						{
							ImGui::TextUnformatted((bdef != nullptr && !bdef->name.empty())
								? bdef->name.c_str() : "Objet");
							if (bdef != nullptr && !bdef->description.empty())
								ImGui::TextDisabled("%s", bdef->description.c_str());
							ImGui::Separator();
							ImGui::TextDisabled("Clic droit : retirer  |  Glisser : deplacer");
							ImGui::TextDisabled("En sac : %u", bQty);
							ImGui::TextDisabled("Utilisation : barre ceinture (Maj+1..9)");
						}
						else
						{
							ImGui::TextDisabled("Case de ceinture (vide)");
							ImGui::TextDisabled("Glissez un consommable du sac ici");
						}
						ImGui::EndTooltip();
					}
				}
				if (beltEdited)
				{
					m_pendingBeltLayout = std::move(newBelt);
					m_beltLayoutDirty = true;
				}
				statsY = beltY0 + beltCell + 8.0f;
			}

			// Curseur repositionné pour la suite (stats) SOUS la rangée
			// « Ceinture » — avant, les stats s'écrivaient juste sous la cellule
			// Waist (retours 2026-07-20 puis 2026-07-21).
			ImGui::SetCursorScreenPos(ImVec2(bandOrigin.x, statsY));

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

					// Roadmap-3 (2026-07-19) — tout CONSOMMABLE (gâteau, potion,
					// nourriture) se place dans la CEINTURE d'un clic (geste
					// volontaire du joueur ; le serveur valide la possession).
					const bool isCake = (def != nullptr)
						&& def->type == engine::items::ItemType::Consumable;

					// Item ImGui superposé (visuels déjà dessinés via wdl) : survol, clic
					// et SOURCE de glisser-déposer vers les cellules d'équipement.
					// Ceinture v2 (2026-07-20) — les consommables sont AUSSI
					// draggables (cible : cases de la barre ceinture, payload
					// commun LN_EQUIP_ITEM = itemId u32).
					ImGui::SetCursorScreenPos(mn);
					char invId[24];
					std::snprintf(invId, sizeof(invId), "##invcell%d", i);
					ImGui::InvisibleButton(invId, ImVec2(cell, cell));
					if ((equippable || isCake) && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
					{
						ImGui::SetDragDropPayload("LN_EQUIP_ITEM", &s.itemId, sizeof(uint32_t));
						ImGui::TextUnformatted(s.label.empty() ? "Objet" : s.label.c_str());
						ImGui::EndDragDropSource();
					}
					if (ImGui::IsItemHovered() && !s.label.empty())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(s.label.c_str());
						if (def != nullptr)
						{
							if (equippable)
								ImGui::TextDisabled("Emplacement : %s", SlotLabelFr(def->slot));
							AppendBonusTooltipLines(def->bonus);
							if (equippable)
							{
								ImGui::Separator();
								ImGui::TextDisabled("Clic ou glisser vers un slot : équiper");
							}
							if (isCake)
							{
								ImGui::Separator();
								ImGui::TextDisabled("Clic ou glisser : placer dans la ceinture");
							}
						}
						ImGui::EndTooltip();
					}
					// Clic simple (relâché sur l'objet, hors glisser) => équiper. Le serveur
					// choisit le slot réel + valide la possession.
					if (equippable && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
						&& ImGui::GetDragDropPayload() == nullptr)
					{
						m_pendingEquip = PendingEquipAction{
							PendingEquipAction::Kind::Equip, s.itemId, 0u};
					}
					// SP3 — clic sur un gâteau : demande de placement en barre.
					if (isCake && ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
						&& ImGui::GetDragDropPayload() == nullptr)
					{
						m_pendingEquip = PendingEquipAction{
							PendingEquipAction::Kind::SlotCake, s.itemId, 0u};
					}
				}
				// Réserve toute la zone grille pour pousser la bourse en bas (curseur
				// remis en haut de grille : les InvisibleButton l'ont déplacé).
				ImGui::SetCursorScreenPos(cur);
				ImGui::Dummy(ImVec2(gridAvailW, gridAvailH));
			}

			// Bourse or/argent/bronze (base = bronze, cf. CurrencyFormat.h), épinglée en bas.
			// Chaque montant est précédé d'une pastille « pièce » colorée (or / argent /
			// bronze) — retour joueur 2026-07-11. Dessin manuel (aucun asset requis).
			const engine::client::CoinBreakdown coins = engine::client::SplitCoins(model.wallet.gold);
			ImGui::Separator();
			{
				ImDrawList* cdl = ImGui::GetWindowDrawList();
				const float coinR = ImGui::GetTextLineHeight() * 0.42f;
				// Dessine une pastille pièce + le montant, puis avance le curseur.
				auto drawCoin = [&](ImU32 fill, ImU32 rim, uint32_t amount)
				{
					const ImVec2 p = ImGui::GetCursorScreenPos();
					const float cx = p.x + coinR;
					const float cy = p.y + ImGui::GetTextLineHeight() * 0.5f;
					cdl->AddCircleFilled(ImVec2(cx, cy), coinR, fill);
					cdl->AddCircle(ImVec2(cx, cy), coinR, rim, 0, 1.5f);
					ImGui::Dummy(ImVec2(coinR * 2.0f, ImGui::GetTextLineHeight()));
					ImGui::SameLine(0.0f, 5.0f);
					ImGui::Text("%u", amount);
				};
				drawCoin(IM_COL32(255, 215, 0, 255),  IM_COL32(170, 130, 0, 255),  coins.gold);   // or
				ImGui::SameLine(0.0f, 16.0f);
				drawCoin(IM_COL32(205, 208, 214, 255), IM_COL32(140, 143, 150, 255), coins.silver); // argent
				ImGui::SameLine(0.0f, 16.0f);
				drawCoin(IM_COL32(205, 127, 50, 255),  IM_COL32(140, 85, 30, 255),  coins.bronze); // bronze
			}
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
