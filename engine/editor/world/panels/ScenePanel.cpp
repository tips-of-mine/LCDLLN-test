#include "engine/editor/world/panels/ScenePanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Affiche le placeholder du panneau Scene. M100.1 : juste un texte
	/// indicatif. Capture la taille courante de la zone client pour que
	/// M100.4 / M100.34 puissent y attacher caméra et image offscreen.
	void ScenePanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Scene", &m_visible))
		{
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			m_viewportWidth = static_cast<int>(avail.x);
			m_viewportHeight = static_cast<int>(avail.y);
			ImGui::TextDisabled("Scene viewport — placeholder M100.1.");
			ImGui::TextWrapped(
				"La vue 3D principale du monde en édition apparaîtra ici "
				"à partir de M100.4 (camera) et M100.34 (integration rendu).");
			ImGui::Text("Viewport courant : %d x %d px", m_viewportWidth, m_viewportHeight);
		}
		ImGui::End();
#endif
	}
}
