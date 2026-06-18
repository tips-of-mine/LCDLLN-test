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
			ImGui::Checkbox("Mode edition batiment (le clic ne sculpte PAS le terrain)", &m_editMode);
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
							m_selectedDraft = -1;
							m_previewDirty = true;
							m_recenterRequest = true; // afficher la variante devant la caméra
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
			ImGui::TextDisabled("La piece en cours s'affiche EN DIRECT dans la vue (avant d'ajouter).");
			// Détecte tout changement pour rafraîchir l'aperçu live de la pièce.
			bool pendingChanged = false;
			pendingChanged |= ImGui::DragFloat3("Position locale (m)", m_newPos, 0.1f);
			pendingChanged |= ImGui::DragFloat3("Rotation XYZ (deg)", m_newRot, 0.5f);
			pendingChanged |= ImGui::DragFloat("Echelle", &m_newScale, 0.01f, 0.01f, 100.0f);
			pendingChanged |= ImGui::Checkbox("Solide (collision)", &m_newSolid);
			ImGui::SameLine();
			pendingChanged |= ImGui::DragFloat("Rayon (0=auto)", &m_newCollision, 0.05f, 0.0f, 50.0f);
			// Changement d'asset sélectionné dans l'Asset Browser → rafraîchir.
			const std::string selId = sel ? sel->id : std::string();
			if (selId != m_lastPreviewAssetId) { m_lastPreviewAssetId = selId; pendingChanged = true; }
			if (pendingChanged) m_previewDirty = true;

			if (ImGui::Button("Ajouter la piece") && sel)
			{
				engine::world::instances::BuildingPart part;
				part.gltfRelativePath = sel->gltfRelativePath;
				part.localPosition = { m_newPos[0], m_newPos[1], m_newPos[2] };
				part.localEulerDeg = { m_newRot[0], m_newRot[1], m_newRot[2] };
				part.localScale = m_newScale;
				part.solid = m_newSolid;
				part.collisionRadius = m_newCollision;
				m_draftParts.push_back(part);
				m_selectedDraft = static_cast<int>(m_draftParts.size()) - 1; // sélectionne la nouvelle
				m_previewDirty = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Rafraichir l'apercu")) m_previewDirty = true;

			ImGui::Separator();
			// --- Pièces de la variante en cours (cliquer pour sélectionner) ---
			ImGui::Text("Pieces de la variante : %zu (cliquer pour editer)", m_draftParts.size());
			if (m_selectedDraft >= static_cast<int>(m_draftParts.size())) m_selectedDraft = -1;
			int removeIdx = -1;
			for (size_t i = 0; i < m_draftParts.size(); ++i)
			{
				const auto& pt = m_draftParts[i];
				ImGui::PushID(static_cast<int>(i));
				char label[160];
				std::snprintf(label, sizeof(label), "%s  (%.1f, %.1f, %.1f)  rot(%.0f,%.0f,%.0f)  x%.2f%s",
					pt.gltfRelativePath.c_str(),
					pt.localPosition.x, pt.localPosition.y, pt.localPosition.z,
					pt.localEulerDeg.x, pt.localEulerDeg.y, pt.localEulerDeg.z,
					pt.localScale, pt.solid ? "" : "  [non solide]");
				if (ImGui::Selectable(label, m_selectedDraft == static_cast<int>(i)))
				{
					// Changer la pièce active doit rebâtir l'aperçu pour que le
					// gizmo (cercles X/Y/Z) se replace sur la pièce sélectionnée.
					if (m_selectedDraft != static_cast<int>(i)) m_previewDirty = true;
					m_selectedDraft = static_cast<int>(i);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("X")) removeIdx = static_cast<int>(i);
				ImGui::PopID();
			}
			if (removeIdx >= 0)
			{
				m_draftParts.erase(m_draftParts.begin() + removeIdx);
				if (m_selectedDraft == removeIdx) m_selectedDraft = -1;
				else if (m_selectedDraft > removeIdx) --m_selectedDraft;
				m_previewDirty = true;
			}

			// --- Édition de la pièce sélectionnée (transform X/Y/Z) -----------
			if (m_selectedDraft >= 0 && m_selectedDraft < static_cast<int>(m_draftParts.size()))
			{
				engine::world::instances::BuildingPart& pt = m_draftParts[m_selectedDraft];
				ImGui::Separator();
				ImGui::Text("Piece selectionnee #%d", m_selectedDraft);
				float pos[3] = { pt.localPosition.x, pt.localPosition.y, pt.localPosition.z };
				float rot[3] = { pt.localEulerDeg.x, pt.localEulerDeg.y, pt.localEulerDeg.z };
				float sc = pt.localScale;
				bool changed = false;
				changed |= ImGui::DragFloat3("Position (m)##sel", pos, 0.1f);
				changed |= ImGui::DragFloat3("Rotation XYZ (deg)##sel", rot, 0.5f);
				changed |= ImGui::DragFloat("Echelle##sel", &sc, 0.01f, 0.01f, 100.0f);
				changed |= ImGui::Checkbox("Solide##sel", &pt.solid);
				if (changed)
				{
					pt.localPosition = { pos[0], pos[1], pos[2] };
					pt.localEulerDeg = { rot[0], rot[1], rot[2] };
					pt.localScale = sc;
					m_previewDirty = true;
				}
				if (ImGui::Button("Deselectionner")) { m_selectedDraft = -1; m_previewDirty = true; }
			}

			if (ImGui::Button("Vider la variante"))
			{ m_draftParts.clear(); m_selectedDraft = -1; m_previewDirty = true; }

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

	bool BuildingEditorPanel::ActivePartLocalPos(float out[3]) const
	{
		if (m_selectedDraft >= 0 && m_selectedDraft < static_cast<int>(m_draftParts.size()))
		{
			const auto& p = m_draftParts[m_selectedDraft].localPosition;
			out[0] = p.x; out[1] = p.y; out[2] = p.z;
			return true;
		}
		// Sinon, la pièce en cours de configuration (si un asset est sélectionné).
		if (m_assetBrowser && m_assetBrowser->SelectedAsset())
		{
			out[0] = m_newPos[0]; out[1] = m_newPos[1]; out[2] = m_newPos[2];
			return true;
		}
		return false;
	}

	std::vector<engine::world::instances::BuildingPart>
	BuildingEditorPanel::PartsForPreview() const
	{
		std::vector<engine::world::instances::BuildingPart> out = m_draftParts;
		const assets::AssetCatalogEntry* sel =
			m_assetBrowser ? m_assetBrowser->SelectedAsset() : nullptr;
		if (sel)
		{
			// Pièce EN COURS de configuration (pas encore ajoutée) : affichée en
			// dernier pour voir le résultat avant « Ajouter la piece ».
			engine::world::instances::BuildingPart pending;
			pending.gltfRelativePath = sel->gltfRelativePath;
			pending.localPosition = { m_newPos[0], m_newPos[1], m_newPos[2] };
			pending.localEulerDeg = { m_newRot[0], m_newRot[1], m_newRot[2] };
			pending.localScale = m_newScale;
			pending.solid = m_newSolid;
			pending.collisionRadius = m_newCollision;
			out.push_back(pending);
		}
		return out;
	}
}
