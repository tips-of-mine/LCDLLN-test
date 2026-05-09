#include "src/world_editor/panels/HistoryPanel.h"
#include "src/world_editor/core/CommandStack.h"

#if defined(_WIN32)
#	include "imgui.h"
#	include <cstdio>
#endif

namespace engine::editor::world::panels
{
	/// Rend la window ImGui "History" : bouton "Clear History" puis liste de
	/// `Selectable` (un par entrée de la pile undo, ordre chronologique : la
	/// dernière ligne est l'item courant marqué "active"). Cliquer sur une
	/// ligne plus ancienne (i < lastIndex) appelle `m_stack->RewindTo(i)` qui
	/// annule en cascade jusqu'à elle. La dernière ligne est rendue
	/// sélectionnée mais non-cliquable (c'est l'état actif).
	///
	/// No-op si invisible ou si `m_stack` est nul. Effet de bord : ImGui
	/// state, éventuel `m_stack->Clear()` ou `m_stack->RewindTo`.
	void HistoryPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible || !m_stack) return;
		if (ImGui::Begin("History", &m_visible))
		{
			if (ImGui::Button("Clear History"))
			{
				m_stack->Clear();
			}
			ImGui::Separator();

			const auto entries = m_stack->SnapshotHistory();
			const size_t lastIndex = entries.empty() ? 0u : entries.size() - 1u;

			for (size_t i = 0; i < entries.size(); ++i)
			{
				const auto& e = entries[i];
				ImGui::PushID(static_cast<int>(i));
				char buf[160];
				const bool isActive = (i == lastIndex);
				std::snprintf(buf, sizeof(buf), "%s  (%zu B)%s",
					e.label.c_str(),
					e.bytes,
					isActive ? "  [active]" : "");
				if (ImGui::Selectable(buf, isActive))
				{
					if (!isActive)
					{
						// Cascader Undo jusqu'à ce que la pile undo contienne
						// exactement (i+1) éléments — i.e. la commande sur
						// laquelle on a cliqué reste appliquée mais devient
						// la nouvelle "active".
						m_stack->RewindTo(i + 1);
					}
				}
				ImGui::PopID();
			}
		}
		ImGui::End();
#endif
	}
}
