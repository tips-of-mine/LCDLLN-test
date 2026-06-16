#include "src/world_editor/panels/AssetBrowserPanel.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Recharge la liste d'assets depuis le disque (hors ImGui : utilisable
	/// indépendamment de la plateforme).
	void AssetBrowserPanel::Refresh(const std::string& absoluteDir,
		const std::string& relativePrefix)
	{
		m_entries = assets::ScanPropAssets(absoluteDir, relativePrefix);
	}

	/// Affiche les assets groupés par catégorie ; un clic sélectionne l'asset et
	/// notifie l'observateur (asset actif du PlacementTool).
	void AssetBrowserPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Asset Browser", &m_visible))
		{
			if (m_entries.empty())
			{
				ImGui::TextDisabled("Aucun asset. (Refresh non appele ou dossier vide.)");
			}
			else
			{
				ImGui::Text("%d asset(s)", static_cast<int>(m_entries.size()));
				ImGui::Separator();
				assets::AssetCategory current = assets::AssetCategory::Unknown;
				bool headerOpen = false;
				bool first = true;
				for (const assets::AssetEntry& e : m_entries)
				{
					if (first || e.category != current)
					{
						current = e.category;
						first = false;
						headerOpen = ImGui::CollapsingHeader(
							assets::CategoryLabel(current), ImGuiTreeNodeFlags_DefaultOpen);
					}
					if (!headerOpen) continue;
					const bool selected = (e.relativePath == m_selectedPath);
					ImGui::PushID(e.relativePath.c_str());
					if (ImGui::Selectable(e.fileName.c_str(), selected))
					{
						m_selectedPath = e.relativePath;
						if (m_onPicked) m_onPicked(e.relativePath);
					}
					ImGui::PopID();
				}
			}
		}
		ImGui::End();
#endif
	}
}
