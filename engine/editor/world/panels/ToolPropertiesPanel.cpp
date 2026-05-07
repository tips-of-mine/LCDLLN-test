#include "engine/editor/world/panels/ToolPropertiesPanel.h"

#include "engine/editor/world/SplatPaintTool.h"
#include "engine/editor/world/StampLibrary.h"
#include "engine/editor/world/TerrainBrush.h"
#include "engine/editor/world/TerrainSculptTool.h"
#include "engine/editor/world/TerrainStampTool.h"
#include "engine/editor/world/WorldEditorShell.h"

#if defined(_WIN32)
#	include "imgui.h"
#endif

#include <algorithm>
#include <cstdint>
#include <filesystem>

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

		/// M100.7 — Rend le bandeau de 4 radios pour le mode de stamp
		/// (Add / Replace / Max / Min). `mode` est lu/écrit sur place.
		void RenderStampModeRadios(engine::editor::world::StampMode& mode)
		{
			using engine::editor::world::StampMode;
			struct Entry { const char* label; StampMode m; };
			constexpr Entry entries[] = {
				{ "Add",     StampMode::Add     },
				{ "Replace", StampMode::Replace },
				{ "Max",     StampMode::Max     },
				{ "Min",     StampMode::Min     },
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

		/// M100.7 — Rend le combo des archétypes procéduraux. `kind` est
		/// lu/écrit sur place.
		void RenderProceduralCombo(engine::editor::world::ProceduralStamp& kind)
		{
			using engine::editor::world::ProceduralStamp;
			static const char* kLabels[] = { "Mountain", "Valley", "Crater" };
			int idx = static_cast<int>(kind);
			if (ImGui::Combo("Procedural", &idx, kLabels, 3))
			{
				kind = static_cast<ProceduralStamp>(idx);
			}
		}

		/// M100.10 — Noms hardcodés des 8 layers de la palette M100.9.
		/// Choix arbitraire pour M100.10 : la palette est statique (lue depuis
		/// `layer_palette.json` au boot du client/éditeur) et les noms ne
		/// changent pas entre sessions. Follow-up possible : injecter la
		/// palette du shell pour piocher les noms dynamiquement.
		const char* kSplatLayerNames[8] = {
			"0 dirt",
			"1 grass_dry",
			"2 grass_wet",
			"3 mud",
			"4 sand",
			"5 rock",
			"6 snow",
			"7 lava_cooled",
		};

		/// M100.10 — Rend le panneau Tool Properties pour `SplatPaintTool`.
		/// Mode (Manual/Auto-Rules), layer combo, sliders radius/strength/falloff,
		/// + sliders slope/alt si auto-rules + bouton Apply to chunk.
		///
		/// Effet de bord : appelle `tool.SetParams` à chaque changement.
		void RenderSplatPaintParams(engine::editor::world::WorldEditorShell& shell,
			engine::editor::world::SplatPaintTool& tool)
		{
			engine::editor::world::SplatPaintParams params = tool.GetParams();

			ImGui::TextUnformatted("Mode:");
			if (ImGui::RadioButton("Manual", !params.autoRules))
			{
				params.autoRules = false;
			}
			ImGui::SameLine();
			if (ImGui::RadioButton("Auto-Rules", params.autoRules))
			{
				params.autoRules = true;
			}
			ImGui::Separator();

			int layerIdx = static_cast<int>(params.activeLayer);
			if (ImGui::Combo("Active layer", &layerIdx, kSplatLayerNames, 8))
			{
				params.activeLayer = static_cast<uint8_t>(
					std::clamp(layerIdx, 0, 7));
			}

			ImGui::SliderFloat("Radius (m)", &params.radiusMeters, 1.0f, 50.0f, "%.1f");
			ImGui::SliderFloat("Strength",   &params.strength,     0.0f, 1.0f,  "%.2f");
			ImGui::SliderFloat("Falloff",    &params.falloff,      0.0f, 1.0f,  "%.2f");

			if (params.autoRules)
			{
				ImGui::Separator();
				ImGui::TextUnformatted("Auto-Rules");
				ImGui::SliderFloat("Slope min (deg)", &params.slopeMinDeg, 0.0f, 90.0f, "%.1f");
				ImGui::SliderFloat("Slope max (deg)", &params.slopeMaxDeg, 0.0f, 90.0f, "%.1f");
				ImGui::SliderFloat("Alt min (m)",     &params.altMin, -1024.0f, 8192.0f, "%.1f");
				ImGui::SliderFloat("Alt max (m)",     &params.altMax, -1024.0f, 8192.0f, "%.1f");
			}

			tool.SetParams(params);

			// Bouton "Apply to chunk" — uniquement en mode auto-rules.
			// M100.10 : sans sélection courante, on applique sur le chunk (0,0)
			// par défaut. La vraie sélection sera branchée dans un follow-up
			// (M100.x outliner / box-select sur le terrain).
			if (params.autoRules)
			{
				ImGui::Separator();
				if (ImGui::Button("Apply to chunk (0,0)"))
				{
					// La Config nécessaire au tool n'est pas exposée ici.
					// On consigne juste l'intention dans la console et on
					// invoque l'API avec une Config vide via le shell — le
					// branchement final passera par un accesseur Config dans
					// le shell (TODO M100.10 follow-up).
					(void)shell; // évite warning unused si future variante
					ImGui::SameLine();
					ImGui::TextDisabled("(needs Config plumbing — TODO follow-up)");
				}
			}
		}
	}
#endif

	void ToolPropertiesPanel::RefreshStampLibrary()
	{
		m_stampLibrary = engine::editor::world::EnumerateStampLibrary(
			std::filesystem::path(m_stampLibraryDir));
		m_stampLibraryLoaded = true;
		// Reset la sélection si elle dépasse la liste actuelle.
		if (m_stampLibrarySelected >= static_cast<int>(m_stampLibrary.size()))
		{
			m_stampLibrarySelected = 0;
		}
	}

	/// Rend le panneau Tool Properties. M100.6 : si l'outil actif est
	/// TerrainSculpt, rend les paramètres de brosse. M100.7 : si l'outil
	/// actif est TerrainStamp, rend les paramètres de stamp. Sinon,
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
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::TerrainStamp)
			{
				ImGui::TextUnformatted("Terrain Stamp");
				ImGui::Separator();

				// Lazy load de la library la première fois que le panel Stamp
				// est rendu (évite de scanner le disque si l'utilisateur
				// n'utilise jamais le tool).
				if (!m_stampLibraryLoaded)
				{
					RefreshStampLibrary();
				}

				engine::editor::world::TerrainStampTool& tool = m_shell->MutableStampTool();
				engine::editor::world::StampParams params = tool.GetParams();

				// Source : Library / Procedural
				ImGui::TextUnformatted("Source:");
				if (ImGui::RadioButton("Library", !params.useProcedural))
				{
					params.useProcedural = false;
				}
				ImGui::SameLine();
				if (ImGui::RadioButton("Procedural", params.useProcedural))
				{
					params.useProcedural = true;
				}

				if (params.useProcedural)
				{
					RenderProceduralCombo(params.procedural);
				}
				else
				{
					if (m_stampLibrary.empty())
					{
						ImGui::TextDisabled("No PNG found in '%s'.",
							m_stampLibraryDir.c_str());
					}
					else
					{
						// Combo construit dynamiquement depuis la library.
						const char* preview = (m_stampLibrarySelected >= 0 &&
							m_stampLibrarySelected < static_cast<int>(m_stampLibrary.size()))
							? m_stampLibrary[m_stampLibrarySelected].name.c_str()
							: "<none>";
						if (ImGui::BeginCombo("Library", preview))
						{
							for (int i = 0; i < static_cast<int>(m_stampLibrary.size()); ++i)
							{
								const bool selected = (i == m_stampLibrarySelected);
								if (ImGui::Selectable(m_stampLibrary[i].name.c_str(), selected))
								{
									m_stampLibrarySelected = i;
									params.libraryPngPath =
										m_stampLibrary[i].path.string();
								}
								if (selected) ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Refresh"))
					{
						RefreshStampLibrary();
					}
				}

				ImGui::Separator();
				ImGui::SliderFloat("Footprint (m)", &params.footprintMeters, 10.0f,  500.0f, "%.1f");
				ImGui::SliderFloat("Strength (m)",  &params.strengthMeters, -200.0f, 200.0f, "%.1f");
				ImGui::SliderFloat("Rotation Y",    &params.rotationYDeg,   -180.0f, 180.0f, "%.1f");
				ImGui::Separator();
				ImGui::TextUnformatted("Mode:");
				RenderStampModeRadios(params.mode);
				tool.SetParams(params);

				ImGui::Separator();
				const bool hasPreview = tool.HasPreview();
				if (!hasPreview) ImGui::BeginDisabled();
				if (ImGui::Button("Apply"))
				{
					tool.Apply();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel preview"))
				{
					tool.Cancel();
				}
				if (!hasPreview) ImGui::EndDisabled();

				if (!hasPreview)
				{
					ImGui::TextDisabled("Click on the terrain to compute a preview.");
				}
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::SplatPaint)
			{
				ImGui::TextUnformatted("Splat Paint");
				ImGui::Separator();
				RenderSplatPaintParams(*m_shell, m_shell->MutableSplatPaintTool());
			}
			else
			{
				ImGui::TextDisabled("Tool Properties — placeholder M100.1.");
				ImGui::TextWrapped(
					"Les propriétés contextuelles de l'outil actif (sculpting, "
					"painting, placement) apparaîtront ici à mesure que les "
					"outils sont implémentés (M100.5 et suivants). "
					"Appuie sur B pour activer la sculpture terrain (M100.6), "
					"N pour activer le stamp (M100.7), ou P pour activer la "
					"peinture splat (M100.10).");
			}
		}
		ImGui::End();
#endif
	}
}
