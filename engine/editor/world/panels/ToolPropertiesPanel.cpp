#include "engine/editor/world/panels/ToolPropertiesPanel.h"

#include "engine/editor/world/TerrainBrush.h"
#include "engine/editor/world/TerrainSculptTool.h"
#include "engine/editor/world/WorldEditorShell.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

namespace engine::editor::world::panels
{
#if defined(_WIN32)
	namespace
	{
		/// Rend le bandeau de 5 boutons radio pour le mode de la brosse
		/// (M100.6). `mode` est lu/écrit sur place.
		void RenderBrushModeRadios(engine::editor::world::TerrainBrushMode& mode)
		{
			using engine::editor::world::TerrainBrushMode;
			struct Entry { const char* label; TerrainBrushMode m; };
			constexpr Entry entries[] = {
				{ "Raise",   TerrainBrushMode::Raise   },
				{ "Lower",   TerrainBrushMode::Lower   },
				{ "Smooth",  TerrainBrushMode::Smooth  },
				{ "Flatten", TerrainBrushMode::Flatten },
				{ "Noise",   TerrainBrushMode::Noise   },
			};
			for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
			{
				if (i > 0) ImGui::SameLine();
				if (ImGui::RadioButton(entries[i].label, mode == entries[i].m))
				{
					mode = entries[i].m;
				}
			}
		}

		/// Rend les sliders communs (radius/strength/falloff) puis les
		/// paramètres dépendants du mode (noise) et les checkboxes mirror.
		void RenderSculptParams(engine::editor::world::TerrainSculptTool& tool)
		{
			engine::editor::world::TerrainBrushParams params = tool.GetParams();
			RenderBrushModeRadios(params.mode);
			ImGui::Separator();
			ImGui::SliderFloat("Radius (m)",   &params.radiusMeters, 1.0f, 50.0f, "%.1f");
			ImGui::SliderFloat("Strength",     &params.strengthMps,  0.1f, 50.0f, "%.2f");
			ImGui::SliderFloat("Falloff",      &params.falloff,      0.0f, 1.0f,  "%.2f");
			if (params.mode == engine::editor::world::TerrainBrushMode::Noise)
			{
				ImGui::SliderFloat("Noise freq",  &params.noiseFreq, 0.001f, 1.0f, "%.3f");
				int octaves = static_cast<int>(params.noiseOctaves);
				if (ImGui::SliderInt("Noise octaves", &octaves, 1, 6))
				{
					params.noiseOctaves = static_cast<uint8_t>(octaves);
				}
			}
			ImGui::Checkbox("Mirror X", &params.mirrorX);
			ImGui::SameLine();
			ImGui::Checkbox("Mirror Z", &params.mirrorZ);
			tool.SetParams(params);
		}
	}
#endif

	/// Rend le panneau Tool Properties. M100.6 : si l'outil actif est
	/// TerrainSculpt, rend les paramètres de brosse ; sinon, affiche le
	/// placeholder M100.1.
	void ToolPropertiesPanel::Render()
	{
#if defined(_WIN32)
		if (!m_visible) return;
		if (ImGui::Begin("Tool Properties", &m_visible))
		{
			if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::TerrainSculpt)
			{
				ImGui::TextUnformatted("Terrain Sculpt");
				ImGui::Separator();
				RenderSculptParams(m_shell->MutableSculptTool());
			}
			else
			{
				ImGui::TextDisabled("Tool Properties — placeholder M100.1.");
				ImGui::TextWrapped(
					"Les propriétés contextuelles de l'outil actif (sculpting, "
					"painting, placement) apparaîtront ici à mesure que les "
					"outils sont implémentés (M100.5 et suivants). "
					"Appuie sur B pour activer la sculpture terrain (M100.6).");
			}
		}
		ImGui::End();
#endif
	}
}
