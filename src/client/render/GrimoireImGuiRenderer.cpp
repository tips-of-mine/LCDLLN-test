#include "src/client/render/GrimoireImGuiRenderer.h"
#include "src/client/grimoire/GrimoireUi.h"
#include "src/client/gameplay/ActionBarLayout.h" // engine::client::FindSpellInKit
#include "src/client/render/SkillIconCache.h"

#if defined(_WIN32)
#	include "imgui.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace engine::render
{
	namespace
	{
		// Payload drag&drop : on transporte le spellId (chaîne courte, < 64).
		constexpr const char* kSpellPayloadId = "LN_GRIMOIRE_SPELL";
	}

	void GrimoireImGuiRenderer::Render()
	{
		if (m_presenter == nullptr || (!m_embedded && !m_enabled) || !m_presenter->IsInitialized())
		{
			return;
		}
		const auto& state = m_presenter->GetState();

		// Mode embarqué (conteneur CharacterWindow) : pas de fenêtre propre, on
		// dessine directement dans l'onglet courant. Mode autonome : fenêtre centrée.
		bool open = true;
		if (!m_embedded)
		{
			const float panelW = 720.f;
			const float panelH = 540.f;
			const float vpW = (m_viewportW > 0) ? static_cast<float>(m_viewportW) : 1280.f;
			const float vpH = (m_viewportH > 0) ? static_cast<float>(m_viewportH) : 720.f;
			ImGui::SetNextWindowPos(ImVec2((vpW - panelW) * 0.5f, (vpH - panelH) * 0.5f), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowSize(ImVec2(panelW, panelH), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowBgAlpha(0.96f);
			const char* title = state.isCaster ? "Grimoire##ln_grimoire" : "Carnet de techniques##ln_grimoire";
			open = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoCollapse);
		}
		if (open)
		{
			// --- Recherche
			static char searchBuf[64] = {0};
			if (ImGui::InputTextWithHint("##ln_grimoire_search", "Rechercher un sort...", searchBuf, sizeof(searchBuf)))
			{
				m_presenter->SetSearchFilter(searchBuf);
			}
			ImGui::Separator();

			ImGui::Columns(2, "##ln_grimoire_cols", true);

			// --- Colonne gauche : liste défilante des sorts (source du drag).
			ImGui::TextDisabled("%zu sorts connus", state.spells.size());
			ImGui::BeginChild("##ln_grimoire_list", ImVec2(0, 0), true);
			for (const engine::client::SpellDisplay& spell : state.spells)
			{
				// Filtre de recherche (insensible à la casse).
				if (!state.searchFilter.empty())
				{
					std::string lname = spell.name;
					std::transform(lname.begin(), lname.end(), lname.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (lname.find(state.searchFilter) == std::string::npos)
					{
						continue;
					}
				}
				ImGui::PushID(spell.spellId.c_str());
				// Icône du sort à gauche du libellé (si disponible).
				if (m_iconCache != nullptr && !spell.iconPath.empty())
				{
					const uint64_t texId = m_iconCache->GetOrLoad(spell.iconPath);
					if (texId != 0)
					{
						ImGui::Image(static_cast<ImTextureID>(texId), ImVec2(22.0f, 22.0f));
						ImGui::SameLine();
					}
				}
				const std::string label = spell.name + "  (coût " + std::to_string(spell.resourceCostPercent)
					+ "%, CD " + std::to_string(spell.cooldownMs / 1000u) + "s)";
				ImGui::Selectable(label.c_str());
				if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
				{
					ImGui::SetDragDropPayload(kSpellPayloadId, spell.spellId.c_str(),
						spell.spellId.size() + 1);
					ImGui::TextUnformatted(spell.name.c_str());
					ImGui::EndDragDropSource();
				}
				ImGui::PopID();
			}
			ImGui::EndChild();

			ImGui::NextColumn();

			// --- Colonne droite : 10 slots (cibles du drop).
			ImGui::TextDisabled("Barre d'action (10 slots)");
			for (uint32_t slotIndex = 0; slotIndex < state.slots.size(); ++slotIndex)
			{
				ImGui::PushID(static_cast<int>(slotIndex + 1000));
				const std::string& sid = state.slots[slotIndex];
				const engine::client::SpellDisplay* spell =
					sid.empty() ? nullptr : engine::client::FindSpellInKit(state.spells, sid);
				const std::string slotLabel = std::to_string(slotIndex + 1) + ".  "
					+ (spell ? spell->name : std::string("— vide —"));
				ImGui::Button(slotLabel.c_str(), ImVec2(-1.f, 30.f));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kSpellPayloadId))
					{
						const std::string dropped(static_cast<const char*>(payload->Data));
						m_presenter->AssignSlot(slotIndex, dropped);
					}
					ImGui::EndDragDropTarget();
				}
				ImGui::SameLine();
				if (!sid.empty() && ImGui::SmallButton("x"))
				{
					m_presenter->AssignSlot(slotIndex, std::string());
				}
				ImGui::PopID();
			}

			ImGui::Columns(1);
		}
		if (!m_embedded)
			ImGui::End();
	}
}

#else  // !_WIN32 — stub no-op (pas d'ImGui hors client Windows).

namespace engine::render
{
	void GrimoireImGuiRenderer::Render() {}
}

#endif
