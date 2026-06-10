#include "src/world_editor/panels/OutlinerPanel.h"

#include "src/world_editor/scene/EditorSelection.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <cstdint>
#include <cstdio>

namespace engine::editor::world::panels
{
	void OutlinerPanel::RenderKindGroup(const char* header, scene::EntityKind kind)
	{
#if defined(_WIN32)
		if (m_sceneModel == nullptr) return;
		const std::vector<scene::SceneEntity>& entities = m_sceneModel->Entities();

		int count = 0;
		for (const scene::SceneEntity& e : entities)
		{
			if (e.id.kind == kind) ++count;
		}

		char label[160];
		std::snprintf(label, sizeof(label), "%s (%d)", header, count);
		if (!ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		for (const scene::SceneEntity& e : entities)
		{
			if (e.id.kind != kind) continue;
			// Lot 0 — surbrillance sur toute la multi-sélection (et plus
			// seulement le primaire) : on teste l'appartenance au set.
			const bool selected = (m_selection != nullptr) && m_selection->IsSelected(e.id);
			// ID ImGui unique : combine kind (bits hauts) et index.
			ImGui::PushID(static_cast<int>(e.id.index)
				| (static_cast<int>(kind) << 24));
			if (ImGui::Selectable(e.label.c_str(), selected))
			{
				if (m_selection != nullptr)
				{
					// Ctrl+clic : bascule l'entité dans le set (multi) ;
					// clic simple : sélection mono.
					if (ImGui::GetIO().KeyCtrl)
						m_selection->ToggleInSelection(e.id);
					else
						m_selection->Select(e.id);
				}
			}
			ImGui::PopID();
		}
#else
		(void)header;
		(void)kind;
#endif
	}

	void OutlinerPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Outliner", &m_visible))
		{
			if (m_sceneModel == nullptr)
			{
				ImGui::TextDisabled("Outliner — aucun modele de scene lie.");
			}
			else
			{
				ImGui::Text("%d entite(s) dans la zone",
					static_cast<int>(m_sceneModel->Entities().size()));
				ImGui::Separator();
				RenderKindGroup("Terrain", scene::EntityKind::Terrain);
				RenderKindGroup("Instances (props / arbres)", scene::EntityKind::LayoutInstance);
				RenderKindGroup("Volumes (grottes / arches / surplombs)", scene::EntityKind::MeshInsert);
				RenderKindGroup("Donjons", scene::EntityKind::DungeonPortal);
			}
		}
		ImGui::End();
#endif
	}
}
