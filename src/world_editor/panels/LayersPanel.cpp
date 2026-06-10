#include "src/world_editor/panels/LayersPanel.h"

#include "src/world_editor/core/WorldEditorShell.h"
#include "src/world_editor/LayersDocument.h"
#include "src/world_editor/scene/EditorSelection.h"

#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
	/// Rend le panneau des 16 calques (Lot 0). Effet de bord : crée la window
	/// ImGui "Layers", mute le `LayersDocument` du shell (visibilité, verrou,
	/// nom, couleur, assignement). Doit être appelée en main thread (ImGui).
	void LayersPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Layers", &m_visible))
		{
			if (m_shell == nullptr)
			{
				ImGui::TextDisabled("Layers — shell non lié.");
				ImGui::End();
				return;
			}

			LayersDocument& doc = m_shell->MutableLayersDocument();

			// Bouton d'assignement de la sélection au calque cible.
			ImGui::SliderInt("Calque cible", &m_assignTargetLayer, 0, kLayerCount - 1);
			if (ImGui::Button("Assigner la sélection à ce calque"))
			{
				const auto& sel = m_shell->GetSelection().SelectedSet();
				for (const engine::editor::scene::EntityId& id : sel)
				{
					const uint64_t key = m_shell->EntityKeyFor(id);
					if (key != 0ull)
						doc.AssignEntity(key, static_cast<uint8_t>(m_assignTargetLayer));
				}
			}
			ImGui::Separator();

			// Liste des 16 calques : nom, visibilité, verrou, couleur.
			for (uint8_t i = 0; i < kLayerCount; ++i)
			{
				Layer& layer = doc.LayerAt(i);
				ImGui::PushID(static_cast<int>(i));

				bool visible = layer.visible;
				if (ImGui::Checkbox("##vis", &visible)) doc.SetVisible(i, visible);
				ImGui::SameLine();

				bool locked = layer.locked;
				if (ImGui::Checkbox("##lock", &locked)) doc.SetLocked(i, locked);
				ImGui::SameLine();

				char buf[64];
				std::snprintf(buf, sizeof(buf), "%s", layer.name.c_str());
				if (ImGui::InputText("##name", buf, sizeof(buf)))
					doc.SetLayerName(i, buf);

				ImGui::SameLine();
				float col[4] = {
					((layer.overlayColorRgba >> 24) & 0xFF) / 255.0f,
					((layer.overlayColorRgba >> 16) & 0xFF) / 255.0f,
					((layer.overlayColorRgba >>  8) & 0xFF) / 255.0f,
					( layer.overlayColorRgba        & 0xFF) / 255.0f };
				if (ImGui::ColorEdit4("##col", col, ImGuiColorEditFlags_NoInputs))
				{
					layer.overlayColorRgba =
						(static_cast<uint32_t>(col[0] * 255.0f) << 24) |
						(static_cast<uint32_t>(col[1] * 255.0f) << 16) |
						(static_cast<uint32_t>(col[2] * 255.0f) <<  8) |
						(static_cast<uint32_t>(col[3] * 255.0f));
				}

				ImGui::PopID();
			}
		}
		ImGui::End();
#endif
	}
}
