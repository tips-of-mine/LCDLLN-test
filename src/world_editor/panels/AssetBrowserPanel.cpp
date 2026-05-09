#include "src/world_editor/world/panels/AssetBrowserPanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Affiche le placeholder du panneau Asset Browser. M100.1 : juste un
	/// texte indicatif décrivant le rôle prévu (ressources du monde).
	void AssetBrowserPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Asset Browser", &m_visible))
		{
			ImGui::TextDisabled("Asset Browser — placeholder M100.1.");
			ImGui::TextWrapped(
				"Les ressources disponibles (meshes, textures, sons, matériaux) "
				"apparaîtront ici dans les tickets dédiés au chargement assets.");
		}
		ImGui::End();
#endif
	}
}
