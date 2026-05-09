#include "src/world_editor/panels/InspectorPanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Affiche le placeholder du panneau Inspector. M100.1 : juste un texte
	/// indicatif décrivant le rôle prévu (sélection courante).
	void InspectorPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Inspector", &m_visible))
		{
			ImGui::TextDisabled("Inspector — placeholder M100.1.");
			ImGui::TextWrapped(
				"Les propriétés de la sélection courante apparaîtront ici dès "
				"M100.34 (Selection / Layers).");
		}
		ImGui::End();
#endif
	}
}
