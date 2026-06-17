#include "src/world_editor/panels/AssetBrowserPanel.h"

#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	void AssetBrowserPanel::EnsureLoaded()
	{
		if (m_loaded) return;
		std::string err;
		m_catalog.LoadFromContent(m_contentRoot, err);
		m_loaded = true; // on ne retente pas chaque frame même si vide/erreur
	}

	namespace
	{
		/// Recherche insensible à la casse de \p needle dans \p hay.
		bool ContainsCI(const std::string& hay, const std::string& needle)
		{
			if (needle.empty()) return true;
			auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
				[](char a, char b) {
					return std::tolower(static_cast<unsigned char>(a)) ==
					       std::tolower(static_cast<unsigned char>(b));
				});
			return it != hay.end();
		}
	}

	void AssetBrowserPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		EnsureLoaded();
		if (ImGui::Begin("Asset Browser", &m_visible))
		{
			ImGui::Text("Catalogue : %zu asset(s)", m_catalog.Size());
			ImGui::SameLine();
			if (ImGui::SmallButton("Recharger")) { m_loaded = false; EnsureLoaded(); }

			// Filtre catégorie.
			const std::vector<std::string> cats = m_catalog.Categories();
			const std::string current = m_categoryFilter.empty() ? "Toutes" : m_categoryFilter;
			if (ImGui::BeginCombo("Categorie", current.c_str()))
			{
				if (ImGui::Selectable("Toutes", m_categoryFilter.empty()))
					m_categoryFilter.clear();
				for (const std::string& c : cats)
				{
					if (ImGui::Selectable(c.c_str(), m_categoryFilter == c))
						m_categoryFilter = c;
				}
				ImGui::EndCombo();
			}

			ImGui::InputText("Recherche", m_search, sizeof(m_search));
			const std::string search = m_search;

			ImGui::Separator();
			if (ImGui::BeginChild("asset_list", ImVec2(0, 0), true))
			{
				for (const assets::AssetCatalogEntry& e : m_catalog.Entries())
				{
					if (!m_categoryFilter.empty() && e.category != m_categoryFilter) continue;
					if (!ContainsCI(e.displayName, search) && !ContainsCI(e.id, search)) continue;
					const bool selected = (e.id == m_selectedId);
					std::string label = "[" + e.category + "] " + e.displayName;
					if (ImGui::Selectable(label.c_str(), selected))
						m_selectedId = e.id;
				}
			}
			ImGui::EndChild();

			if (!m_selectedId.empty())
				ImGui::TextWrapped("Selectionne : %s", m_selectedId.c_str());
		}
		ImGui::End();
#endif
	}
}
