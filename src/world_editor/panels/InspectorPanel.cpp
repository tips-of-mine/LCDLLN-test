#include "src/world_editor/panels/InspectorPanel.h"

#include "src/world_editor/scene/EditorSelection.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

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
					if (e->hasTransform)
					{
						// Lecture seule a ce stade (edition undoable -> increment D suivant).
						ImGui::TextDisabled("Transform (lecture seule)");
						ImGui::Text("Position : %.2f, %.2f, %.2f m",
							static_cast<double>(e->transform.position.x),
							static_cast<double>(e->transform.position.y),
							static_cast<double>(e->transform.position.z));
						ImGui::Text("Rotation : %.1f, %.1f, %.1f deg",
							static_cast<double>(e->transform.eulerDeg.x),
							static_cast<double>(e->transform.eulerDeg.y),
							static_cast<double>(e->transform.eulerDeg.z));
						ImGui::Text("Echelle  : %.3f",
							static_cast<double>(e->transform.uniformScale));
					}
					else
					{
						ImGui::TextDisabled("(pas de transform editable)");
					}
				}
			}
		}
		ImGui::End();
#endif
	}
}
