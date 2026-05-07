#include "engine/editor/world/panels/OutlinerPanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Affiche le placeholder du panneau Outliner. M100.1 : juste un texte
	/// indicatif décrivant le rôle prévu (hiérarchie des entités).
	void OutlinerPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Outliner", &m_visible))
		{
			ImGui::TextDisabled("Outliner — placeholder M100.1.");
			ImGui::TextWrapped(
				"La hiérarchie des entités du monde (zones, props, lumières) "
				"apparaîtra ici dès M100.34 (Selection / Layers).");
		}
		ImGui::End();
#endif
	}
}
