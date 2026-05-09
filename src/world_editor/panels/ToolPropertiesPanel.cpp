#include "src/world_editor/world/panels/ToolPropertiesPanel.h"

#include "src/world_editor/world/LakeTool.h"
#include "src/world_editor/world/RiverTool.h"
#include "src/world_editor/world/SplatPaintTool.h"
#include "src/world_editor/world/StampLibrary.h"
#include "src/world_editor/world/TerrainBrush.h"
#include "src/world_editor/world/TerrainSculptTool.h"
#include "src/world_editor/world/TerrainStampTool.h"
#include "src/world_editor/world/WaterDocument.h"
#include "src/world_editor/world/WorldEditorShell.h"

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

	namespace
	{
		// M100.13 — État persistant entre frames pour le mini-canvas 2D Lake/River.
		// Un état partagé OK car les deux outils ne sont pas actifs simultanément.
		struct WaterCanvasState
		{
			float boundsHalfMeters = 50.0f;
			float centerWorldX = 0.0f;
			float centerWorldZ = 0.0f;
		};

		// M100.13 — Events souris du mini-canvas 2D, retournés à la frame.
		struct WaterCanvasInput
		{
			bool   leftClicked  = false;
			bool   rightClicked = false;
			float  worldX = 0.0f;
			float  worldZ = 0.0f;
		};

		/// Convertit pixel canvas (0..W, 0..H) → coords monde XZ.
		void PixelToWorld(const WaterCanvasState& s, float w, float h, float px, float py,
			float& outX, float& outZ)
		{
			const float fx = (px / w) * 2.0f - 1.0f;
			const float fy = (py / h) * 2.0f - 1.0f;
			outX = s.centerWorldX + fx * s.boundsHalfMeters;
			outZ = s.centerWorldZ - fy * s.boundsHalfMeters;
		}

		/// Convertit coords monde XZ → pixel canvas.
		void WorldToPixel(const WaterCanvasState& s, float w, float h, float worldX, float worldZ,
			float& outPx, float& outPy)
		{
			const float fx = (worldX - s.centerWorldX) / s.boundsHalfMeters;
			const float fy = (s.centerWorldZ - worldZ) / s.boundsHalfMeters;
			outPx = (fx * 0.5f + 0.5f) * w;
			outPy = (fy * 0.5f + 0.5f) * h;
		}

		/// M100.13 — Rend le canvas 2D top-down dans une ChildWindow ImGui.
		/// Affiche existing scene en gris + currentPolygon en jaune + currentNodes
		/// en cyan. Retourne les events souris (LMB add, RMB cancel).
		WaterCanvasInput RenderTopDownCanvas(
			WaterCanvasState& state,
			const engine::world::water::WaterScene& existingScene,
			const std::vector<engine::math::Vec3>* currentPolygon,
			const std::vector<engine::world::water::RiverNode>* currentNodes)
		{
			WaterCanvasInput input;

#if defined(_WIN32)
			ImGui::SliderFloat("Bounds (m)", &state.boundsHalfMeters, 5.0f, 500.0f, "%.1f");
			ImGui::SameLine();
			if (ImGui::Button("Recenter"))
			{
				state.centerWorldX = 0.0f;
				state.centerWorldZ = 0.0f;
			}

			const ImVec2 canvasSize{ 300.0f, 300.0f };
			ImGui::BeginChild("##waterCanvas", canvasSize, true,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
			const ImVec2 contentSize = ImGui::GetContentRegionAvail();
			const float w = contentSize.x;
			const float h = contentSize.y;

			ImGui::InvisibleButton("##canvasInteract", contentSize);
			const bool hovered = ImGui::IsItemHovered();
			if (hovered)
			{
				const ImVec2 mp = ImGui::GetIO().MousePos;
				const float px = mp.x - canvasMin.x;
				const float py = mp.y - canvasMin.y;
				float wx = 0.0f, wz = 0.0f;
				PixelToWorld(state, w, h, px, py, wx, wz);
				input.worldX = wx;
				input.worldZ = wz;
				ImGui::SetTooltip("world: (%.1f, %.1f)", wx, wz);
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))  input.leftClicked = true;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) input.rightClicked = true;
			}

			ImDrawList* dl = ImGui::GetWindowDrawList();

			// Croix au centre (origine monde)
			float cx = 0.0f, cy = 0.0f;
			WorldToPixel(state, w, h, 0.0f, 0.0f, cx, cy);
			dl->AddLine(ImVec2(canvasMin.x + cx - 5, canvasMin.y + cy),
			            ImVec2(canvasMin.x + cx + 5, canvasMin.y + cy),
			            IM_COL32(255, 255, 255, 100), 1.0f);
			dl->AddLine(ImVec2(canvasMin.x + cx, canvasMin.y + cy - 5),
			            ImVec2(canvasMin.x + cx, canvasMin.y + cy + 5),
			            IM_COL32(255, 255, 255, 100), 1.0f);

			auto drawPolygonClosed = [&](const std::vector<engine::math::Vec3>& poly, ImU32 color)
			{
				if (poly.size() < 2) return;
				for (size_t i = 0; i < poly.size(); ++i)
				{
					const auto& a = poly[i];
					const auto& b = poly[(i + 1) % poly.size()];
					float ax, ay, bx, by;
					WorldToPixel(state, w, h, a.x, a.z, ax, ay);
					WorldToPixel(state, w, h, b.x, b.z, bx, by);
					dl->AddLine(ImVec2(canvasMin.x + ax, canvasMin.y + ay),
					            ImVec2(canvasMin.x + bx, canvasMin.y + by),
					            color, 1.5f);
				}
				for (const auto& v : poly)
				{
					float px, py;
					WorldToPixel(state, w, h, v.x, v.z, px, py);
					dl->AddCircleFilled(ImVec2(canvasMin.x + px, canvasMin.y + py), 3.0f, color);
				}
			};

			auto drawPolyline = [&](const std::vector<engine::math::Vec3>& nodes, ImU32 color)
			{
				for (size_t i = 0; i + 1 < nodes.size(); ++i)
				{
					const auto& a = nodes[i];
					const auto& b = nodes[i + 1];
					float ax, ay, bx, by;
					WorldToPixel(state, w, h, a.x, a.z, ax, ay);
					WorldToPixel(state, w, h, b.x, b.z, bx, by);
					dl->AddLine(ImVec2(canvasMin.x + ax, canvasMin.y + ay),
					            ImVec2(canvasMin.x + bx, canvasMin.y + by),
					            color, 1.5f);
				}
				for (const auto& v : nodes)
				{
					float px, py;
					WorldToPixel(state, w, h, v.x, v.z, px, py);
					dl->AddCircleFilled(ImVec2(canvasMin.x + px, canvasMin.y + py), 3.0f, color);
				}
			};

			// Existing lakes en gris clair
			for (const auto& lake : existingScene.lakes)
				drawPolygonClosed(lake.polygon, IM_COL32(180, 180, 180, 200));

			// Existing rivers en gris foncé
			for (const auto& river : existingScene.rivers)
			{
				std::vector<engine::math::Vec3> positions;
				positions.reserve(river.nodes.size());
				for (const auto& n : river.nodes) positions.push_back(n.position);
				drawPolyline(positions, IM_COL32(140, 140, 180, 200));
			}

			// Current polygon (lake en cours) en jaune
			if (currentPolygon && !currentPolygon->empty())
				drawPolygonClosed(*currentPolygon, IM_COL32(255, 220, 80, 255));

			// Current river nodes en cyan
			if (currentNodes && !currentNodes->empty())
			{
				std::vector<engine::math::Vec3> positions;
				positions.reserve(currentNodes->size());
				for (const auto& n : *currentNodes) positions.push_back(n.position);
				drawPolyline(positions, IM_COL32(80, 220, 255, 255));
			}

			ImGui::EndChild();
#else
			(void)state; (void)existingScene; (void)currentPolygon; (void)currentNodes;
#endif
			return input;
		}

		// M100.13 — State persistant partagé entre Lake et River (ne sont pas actifs
		// en même temps).
		WaterCanvasState g_waterCanvasState;
	}

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

	void ToolPropertiesPanel::RenderLakeParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::LakeTool& tool)
	{
#if defined(_WIN32)
		ImGui::Text("Lake Tool — M100.13");
		ImGui::Separator();

		ImGui::Text("Default values for next lake :");
		ImGui::SliderFloat("Water Level Y", &tool.MutableWaterLevelY(), -50.0f, 50.0f, "%.3f");
		ImGui::ColorEdit3("Bottom Color", &tool.MutableBottomColor().x);
		ImGui::SliderFloat("Turbidity", &tool.MutableTurbidity(), 0.0f, 1.0f, "%.2f");
		ImGui::Separator();

		ImGui::Text("Current polygon : %zu points", tool.GetPointCount());
		const bool canClose = tool.GetPointCount() >= 3;
		ImGui::BeginDisabled(!canClose);
		if (ImGui::Button("Close polygon (commit lake)"))
			tool.ClosePolygon();
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			tool.Cancel();
		ImGui::Separator();

		ImGui::Text("Top-Down Canvas (LMB add point, RMB cancel)");
		const auto& currentPoints = tool.GetCurrentPoints();
		const auto canvasInput = RenderTopDownCanvas(
			g_waterCanvasState,
			shell.GetWaterDocument().Get(),
			&currentPoints,
			nullptr);
		if (canvasInput.leftClicked)
			tool.AddPoint(canvasInput.worldX, canvasInput.worldZ);
		if (canvasInput.rightClicked)
			tool.Cancel();

		ImGui::Separator();
		ImGui::Text("Existing lakes (%zu) :", shell.GetWaterDocument().Get().lakes.size());
		if (ImGui::BeginTable("##lakes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Pts", ImGuiTableColumnFlags_WidthFixed, 50);
			ImGui::TableSetupColumn("Y-level", ImGuiTableColumnFlags_WidthFixed, 70);
			ImGui::TableSetupColumn("");
			ImGui::TableHeadersRow();
			const auto& lakes = shell.GetWaterDocument().Get().lakes;
			for (size_t i = 0; i < lakes.size(); ++i)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(lakes[i].name.c_str());
				ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", lakes[i].polygon.size());
				ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f", lakes[i].waterLevelY);
				ImGui::TableSetColumnIndex(3); ImGui::TextDisabled("(no del)");
			}
			ImGui::EndTable();
		}
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderRiverParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::RiverTool& tool)
	{
#if defined(_WIN32)
		ImGui::Text("River Tool — M100.13");
		ImGui::Separator();

		ImGui::Text("Default values for next node :");
		ImGui::SliderFloat("Width (m)", &tool.MutableDefaultWidth(), 0.5f, 30.0f, "%.2f");
		ImGui::SliderFloat("Depth (m)", &tool.MutableDefaultDepth(), 0.1f, 10.0f, "%.2f");
		ImGui::Separator();

		ImGui::Text("Current river : %zu nodes", tool.GetNodeCount());
		const bool canEnd = tool.GetNodeCount() >= 2;
		ImGui::BeginDisabled(!canEnd);
		if (ImGui::Button("End spline (commit river)"))
			tool.EndSpline();
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			tool.Cancel();
		ImGui::Separator();

		ImGui::Text("Top-Down Canvas (LMB add node, RMB cancel)");
		const auto& currentNodes = tool.GetCurrentNodes();
		const auto canvasInput = RenderTopDownCanvas(
			g_waterCanvasState,
			shell.GetWaterDocument().Get(),
			nullptr,
			&currentNodes);
		if (canvasInput.leftClicked)
			tool.AddNode(canvasInput.worldX, canvasInput.worldZ);
		if (canvasInput.rightClicked)
			tool.Cancel();

		ImGui::Separator();
		ImGui::Text("Existing rivers (%zu) :", shell.GetWaterDocument().Get().rivers.size());
		if (ImGui::BeginTable("##rivers", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Nodes", ImGuiTableColumnFlags_WidthFixed, 60);
			ImGui::TableSetupColumn("");
			ImGui::TableHeadersRow();
			const auto& rivers = shell.GetWaterDocument().Get().rivers;
			for (size_t i = 0; i < rivers.size(); ++i)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(rivers[i].name.c_str());
				ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", rivers[i].nodes.size());
				ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("(no del)");
			}
			ImGui::EndTable();
		}
#else
		(void)shell; (void)tool;
#endif
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
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::Lake)
			{
				ImGui::TextUnformatted("Lake");
				ImGui::Separator();
				RenderLakeParams(*m_shell, m_shell->MutableLakeTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::River)
			{
				ImGui::TextUnformatted("River");
				ImGui::Separator();
				RenderRiverParams(*m_shell, m_shell->MutableRiverTool());
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
