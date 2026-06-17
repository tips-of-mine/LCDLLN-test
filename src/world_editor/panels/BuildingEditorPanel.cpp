#include "src/world_editor/panels/BuildingEditorPanel.h"

#include "src/client/world/instances/BuildingTemplateLibrary.h"
#include "src/world_editor/buildings/BuildingDocument.h"
#include "src/world_editor/panels/AssetBrowserPanel.h"

#include <cstdio>
#include <string>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	void BuildingEditorPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Building Editor", &m_visible))
		{
			ImGui::TextWrapped(
				"Compose une variante de batiment a partir des assets de l'Asset "
				"Browser, enregistre-la dans le fichier de son type, puis pose une "
				"reference sur la carte.");
			ImGui::Separator();

			// --- Charger une variante existante (pour la modifier) ----------
			ImGui::TextUnformatted("Charger une variante existante (ex: l'auberge) :");
			if (m_library)
			{
				const auto& templates = m_library->Templates();
				if (ImGui::BeginCombo("Type a charger",
					m_loadType.empty() ? "(choisir)" : m_loadType.c_str()))
				{
					for (const auto& t : templates)
						if (ImGui::Selectable(t.type.c_str(), m_loadType == t.type))
						{ m_loadType = t.type; m_loadVariant.clear(); }
					ImGui::EndCombo();
				}
				const engine::world::instances::BuildingTemplate* selT =
					m_library->FindType(m_loadType);
				if (selT)
				{
					if (ImGui::BeginCombo("Variante a charger",
						m_loadVariant.empty() ? "(choisir)" : m_loadVariant.c_str()))
					{
						for (const auto& v : selT->variants)
							if (ImGui::Selectable(v.id.c_str(), m_loadVariant == v.id))
								m_loadVariant = v.id;
						ImGui::EndCombo();
					}
					if (ImGui::Button("Charger dans l'editeur"))
					{
						const engine::world::instances::BuildingVariant* v =
							selT->FindVariant(m_loadVariant);
						if (v)
						{
							m_draftParts = v->parts;
							std::snprintf(m_typeBuf, sizeof(m_typeBuf), "%s", selT->type.c_str());
							std::snprintf(m_typeNameBuf, sizeof(m_typeNameBuf), "%s", selT->displayName.c_str());
							std::snprintf(m_variantBuf, sizeof(m_variantBuf), "%s", v->id.c_str());
							std::snprintf(m_variantNameBuf, sizeof(m_variantNameBuf), "%s", v->displayName.c_str());
							m_previewDirty = true;
							m_status = "Variante chargee : " + v->id + " (" +
								std::to_string(v->parts.size()) + " pieces). Modifie puis Enregistre.";
						}
					}
				}
			}
			else
			{
				ImGui::TextDisabled("(bibliotheque indisponible)");
			}
			ImGui::Separator();

			// --- Type / variante -------------------------------------------
			ImGui::InputText("Type (fichier)", m_typeBuf, sizeof(m_typeBuf));
			ImGui::InputText("Nom du type", m_typeNameBuf, sizeof(m_typeNameBuf));
			ImGui::InputText("Id variante", m_variantBuf, sizeof(m_variantBuf));
			ImGui::InputText("Nom variante", m_variantNameBuf, sizeof(m_variantNameBuf));

			ImGui::Separator();
			// --- Ajout de pièce --------------------------------------------
			const assets::AssetCatalogEntry* sel =
				m_assetBrowser ? m_assetBrowser->SelectedAsset() : nullptr;
			ImGui::Text("Asset selectionne : %s",
				sel ? sel->displayName.c_str() : "(aucun — choisir dans Asset Browser)");
			ImGui::DragFloat3("Position locale (m)", m_newPos, 0.1f);
			ImGui::DragFloat("Rotation Y (deg)", &m_newYaw, 0.5f);
			ImGui::DragFloat("Echelle", &m_newScale, 0.01f, 0.01f, 100.0f);
			ImGui::Checkbox("Solide (collision)", &m_newSolid);
			ImGui::SameLine();
			ImGui::DragFloat("Rayon (0=auto)", &m_newCollision, 0.05f, 0.0f, 50.0f);

			if (ImGui::Button("Ajouter la piece") && sel)
			{
				engine::world::instances::BuildingPart part;
				part.gltfRelativePath = sel->gltfRelativePath;
				part.localPosition = { m_newPos[0], m_newPos[1], m_newPos[2] };
				part.localEulerDeg = { 0.0f, m_newYaw, 0.0f };
				part.localScale = m_newScale;
				part.solid = m_newSolid;
				part.collisionRadius = m_newCollision;
				m_draftParts.push_back(part);
				m_previewDirty = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Rafraichir l'apercu")) m_previewDirty = true;

			ImGui::Separator();
			// --- Pièces de la variante en cours ----------------------------
			ImGui::Text("Pieces de la variante : %zu", m_draftParts.size());
			int removeIdx = -1;
			for (size_t i = 0; i < m_draftParts.size(); ++i)
			{
				const auto& pt = m_draftParts[i];
				ImGui::PushID(static_cast<int>(i));
				ImGui::BulletText("%s  (%.1f, %.1f, %.1f)  yaw %.0f  x%.2f%s",
					pt.gltfRelativePath.c_str(),
					pt.localPosition.x, pt.localPosition.y, pt.localPosition.z,
					pt.localEulerDeg.y, pt.localScale, pt.solid ? "" : "  [non solide]");
				ImGui::SameLine();
				if (ImGui::SmallButton("X")) removeIdx = static_cast<int>(i);
				ImGui::PopID();
			}
			if (removeIdx >= 0)
			{
				m_draftParts.erase(m_draftParts.begin() + removeIdx);
				m_previewDirty = true;
			}

			if (ImGui::Button("Vider la variante")) { m_draftParts.clear(); m_previewDirty = true; }

			ImGui::Separator();
			// --- Enregistrer la variante dans le fichier du type -----------
			if (ImGui::Button("Enregistrer la variante"))
			{
				if (!m_library) { m_status = "Erreur : bibliotheque non initialisee."; }
				else if (m_typeBuf[0] == '\0' || m_variantBuf[0] == '\0')
				{ m_status = "Type et id variante requis."; }
				else
				{
					engine::world::instances::BuildingVariant var;
					var.id = m_variantBuf;
					var.displayName = (m_variantNameBuf[0] != '\0') ? m_variantNameBuf : m_variantBuf;
					var.parts = m_draftParts;
					std::string err;
					if (m_library->SaveVariant(m_contentRoot, m_typeBuf, m_typeNameBuf, var, err))
						m_status = std::string("Variante enregistree dans buildings/templates/") + m_typeBuf + ".json";
					else
						m_status = "Echec sauvegarde : " + err;
				}
			}

			ImGui::Separator();
			// --- Poser une référence sur la carte --------------------------
			ImGui::Text("Poser sur la carte (reference vers la variante)");
			ImGui::DragFloat2("Position monde X/Z", m_placePos, 0.5f);
			ImGui::DragFloat("Yaw monde (deg)", &m_placeYaw, 0.5f);
			ImGui::DragFloat("Echelle groupe", &m_placeScale, 0.01f, 0.01f, 100.0f);
			if (ImGui::Button("Poser sur la carte"))
			{
				if (!m_doc) { m_status = "Erreur : document non initialise."; }
				else if (m_typeBuf[0] == '\0' || m_variantBuf[0] == '\0')
				{ m_status = "Type et id variante requis pour poser."; }
				else
				{
					engine::world::instances::BuildingPlacement pl;
					pl.templateType = m_typeBuf;
					pl.variantId = m_variantBuf;
					pl.displayName = (m_variantNameBuf[0] != '\0') ? m_variantNameBuf : m_variantBuf;
					pl.worldPosition = { m_placePos[0], 0.0f, m_placePos[1] };
					pl.worldYawDeg = m_placeYaw;
					pl.worldScale = m_placeScale;
					const uint64_t guid = m_doc->Add(pl);
					m_previewDirty = true;
					m_status = "Reference posee (guid " + std::to_string(guid) +
						"). Pense a sauvegarder la zone.";
				}
			}

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
