#include "engine/editor/world/panels/ConsolePanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Affiche le placeholder du panneau Console. M100.1 : juste un texte
	/// indicatif. Le branchement au sink engine::core::Log sera fait dans
	/// un ticket ultérieur (mécanisme de sink non exposé par Log.h pour
	/// l'instant).
	void ConsolePanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Console", &m_visible))
		{
			ImGui::TextDisabled("Console — placeholder M100.1.");
			ImGui::TextWrapped(
				"Les lignes LOG_*(EditorWorld, ...) apparaîtront ici dès "
				"l'intégration du sink engine::core::Log (M100.2 ou plus tard).");
		}
		ImGui::End();
#endif
	}
}
