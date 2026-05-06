#include "engine/editor/world/panels/ToolPropertiesPanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Affiche le placeholder du panneau Tool Properties. M100.1 : juste un
	/// texte indicatif. Les outils s'enregistrent ici à mesure qu'ils sont
	/// implémentés.
	void ToolPropertiesPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Tool Properties", &m_visible))
		{
			ImGui::TextDisabled("Tool Properties — placeholder M100.1.");
			ImGui::TextWrapped(
				"Les propriétés contextuelles de l'outil actif (sculpting, "
				"painting, placement) apparaîtront ici à mesure que les "
				"outils sont implémentés (M100.5 et suivants).");
		}
		ImGui::End();
#endif
	}
}
