#include "src/world_editor/panels/InspectorPanel.h"

#include "src/world_editor/scene/EditorSelection.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <memory>

namespace engine::editor::world::panels
{
	namespace
	{
		/// Libellé français lisible pour un type d'entité (affichage Inspector).
		const char* KindLabel(scene::EntityKind k)
		{
			switch (k)
			{
				case scene::EntityKind::Terrain:        return "Terrain";
				case scene::EntityKind::Water:          return "Eau";
				case scene::EntityKind::MeshInsert:     return "Volume (grotte / arche / surplomb)";
				case scene::EntityKind::DungeonPortal:  return "Donjon";
				case scene::EntityKind::LayoutInstance: return "Instance (prop / arbre)";
				case scene::EntityKind::None:           return "(aucun)";
			}
			return "?";
		}
	}

	void InspectorPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Inspector", &m_visible))
		{
			const bool hasSel = (m_selection != nullptr) && m_selection->HasSelection();
			if (!hasSel || m_sceneModel == nullptr)
			{
				ImGui::TextDisabled("Aucune entite selectionnee.");
				ImGui::TextWrapped(
					"Selectionnez une entite dans l'Outliner pour afficher ses proprietes.");
			}
			else
			{
				const scene::SceneEntity* e = m_sceneModel->Find(m_selection->Current());
				if (e == nullptr)
				{
					ImGui::TextDisabled("Entite introuvable (liste recalculee cette frame).");
				}
				else
				{
					ImGui::Text("Type : %s", KindLabel(e->id.kind));
					ImGui::Text("Nom  : %s", e->label.c_str());
					ImGui::Separator();
					if (!e->hasTransform)
					{
						ImGui::TextDisabled("(pas de transform editable)");
					}
					else
					{
						// État courant (= document, le modèle de scène est
						// reconstruit chaque frame par l'Engine).
						const scene::EntityTransform cur = e->transform;
						float pos[3]   = { cur.position.x, cur.position.y, cur.position.z };
						float yaw      = cur.eulerDeg.y;
						float scale    = cur.uniformScale;

						bool changed = false;
						changed |= ImGui::DragFloat3("Position (m)", pos, 0.1f);
						changed |= ImGui::DragFloat("Rotation Y (deg)", &yaw, 0.5f);
						changed |= ImGui::DragFloat("Echelle", &scale, 0.01f, 0.01f, 1000.0f);

						const bool canEdit = (m_commandStack != nullptr)
							&& (m_writer != nullptr) && static_cast<bool>(*m_writer);
						if (changed && canEdit)
						{
							scene::EntityTransform next = cur;
							next.position.x   = pos[0];
							next.position.y   = pos[1];
							next.position.z   = pos[2];
							next.eulerDeg.y   = yaw;
							next.uniformScale = scale;
							// Push chaque frame de drag : les commandes consecutives
							// d'une meme entite fusionnent (1 item d'historique).
							m_commandStack->Push(std::make_unique<SetEntityTransformCommand>(
								e->id, cur, next, *m_writer));
						}
						else if (!canEdit)
						{
							ImGui::TextDisabled("(edition indisponible : pile/ecriture non branchees)");
						}
					}
				}
			}
		}
		ImGui::End();
#endif
	}
}
