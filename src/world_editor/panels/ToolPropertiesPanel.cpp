#include "src/world_editor/panels/ToolPropertiesPanel.h"

#include "src/shared/core/Log.h"

#include "src/world_editor/terrain/erosion/HydraulicErosionTool.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionTool.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/modes/EditorModeRegistry.h"
#include "src/world_editor/presets/ToolPresetApply.h"
#include "src/world_editor/ui/PresetDropdownWidget.h"
#include "src/world_editor/volumes/arches/ArchTool.h"
#include "src/world_editor/volumes/bridge/Phase11Validator.h"
#include "src/world_editor/volumes/bridge/VMapBridge.h"
#include "src/world_editor/volumes/caves/CaveTool.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalTool.h"
#include "src/world_editor/volumes/overhangs/OverhangTool.h"
#include "src/world_editor/water/CoastlineEditorTool.h"
#include "src/world_editor/water/LakeTool.h"
#include "src/world_editor/water/RiverNetworkTool.h"
#include "src/world_editor/water/RiverTool.h"
#include "src/world_editor/splat/SplatPaintTool.h"
#include "src/world_editor/terrain/MountainRangeTool.h"
#include "src/world_editor/terrain/StampLibrary.h"
#include "src/world_editor/terrain/TerrainBrush.h"
#include "src/world_editor/terrain/TerrainSculptTool.h"
#include "src/world_editor/terrain/TerrainStampTool.h"
#include "src/world_editor/terrain/ValleyChainTool.h"
#include "src/world_editor/water/WaterDocument.h"
#include "src/world_editor/core/WorldEditorShell.h"

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
		/// M100.45 Phase B — outil migré : dropdown de presets + split
		/// Simple/Advanced. Simple = mode + rayon + force ; Advanced +=
		/// falloff, bruit, mirror.
		void RenderSculptParams(engine::editor::world::TerrainSculptTool& tool)
		{
			const bool advanced =
				engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
					== engine::editor::world::modes::EditorMode::Advanced;

			engine::editor::world::TerrainBrushParams params = tool.GetParams();

			// M100.45 A.6 — dropdown de presets (tool_presets/sculpt.json).
			// Le callback s'exécute synchrone : il mute la copie locale
			// `params`, les sliders ci-dessous reflètent la nouvelle valeur.
			engine::editor::world::ui::RenderPresetDropdown(
				"sculpt",
				[&params](const engine::editor::world::presets::ToolPreset& preset) {
					engine::editor::world::presets::ApplySculptPreset(params, preset);
				});

			RenderBrushModeRadios(params.mode);
			ImGui::Separator();
			ImGui::SliderFloat("Radius (m)",   &params.radiusMeters, 1.0f, 50.0f, "%.1f");
			ImGui::SliderFloat("Strength",     &params.strengthMps,  0.1f, 50.0f, "%.2f");

			if (advanced)
			{
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
			}
			else
			{
				ImGui::TextDisabled("Mode Simple — Options > Mode editeur > Avance pour falloff/bruit/mirror.");
			}
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
			// M100.45 Phase B — outil migré : dropdown de presets + split
			// Simple/Advanced. Simple = mode + couche + rayon + force ;
			// Advanced += falloff + filtres auto-rules slope/alt.
			const bool advanced =
				engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
					== engine::editor::world::modes::EditorMode::Advanced;

			engine::editor::world::SplatPaintParams params = tool.GetParams();

			// M100.45 A.6 — dropdown de presets (tool_presets/splat_paint.json).
			engine::editor::world::ui::RenderPresetDropdown(
				"splat_paint",
				[&params](const engine::editor::world::presets::ToolPreset& preset) {
					engine::editor::world::presets::ApplySplatPaintPreset(params, preset);
				});

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

			if (advanced)
			{
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
			}
			else
			{
				ImGui::TextDisabled("Mode Simple — Options > Mode editeur > Avance pour falloff + auto-rules.");
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

	namespace
	{
#if defined(_WIN32)
		/// M100.35 — Helper templaté pour le bloc "Macro polyline" partagé
		/// entre Mountain Range et Valley Chain. La méthode `Apply` / `Cancel`
		/// est identique sur les deux types (interface dupliquée), donc on
		/// peut paramétrer le helper sur le tool concret.
		template <typename Tool>
		void RenderMacroPolylineBlock(Tool& tool, const char* title)
		{
			using engine::editor::world::FlankProfile;
			using engine::editor::world::PolylineMode;
			using engine::editor::world::kMacroPolylineMaxVertices;

			auto& params = tool.MutableParams();

			ImGui::Text("%s", title);
			ImGui::Text("Vertices posés : %zu / max %zu",
				params.vertices.size(), kMacroPolylineMaxVertices);
			ImGui::Separator();

			// Mode polyline (Open / Loop)
			int modeIdx = (params.mode == PolylineMode::Loop) ? 1 : 0;
			static const char* kModeLabels[2] = { "Open", "Loop" };
			if (ImGui::Combo("Mode polyline", &modeIdx, kModeLabels, 2))
			{
				params.mode = (modeIdx == 1) ? PolylineMode::Loop : PolylineMode::Open;
			}

			// Profil flanc (Smoothstep / Linear / Exp)
			int profIdx = static_cast<int>(params.profile);
			static const char* kProfileLabels[3] = { "Smoothstep", "Linear", "Exp" };
			if (ImGui::Combo("Profil flanc", &profIdx, kProfileLabels, 3))
			{
				params.profile = static_cast<FlankProfile>(
					std::clamp(profIdx, 0, 2));
			}

			// Seed + fréquence bruit (globaux à la polyline).
			int seed = static_cast<int>(params.noiseSeed);
			if (ImGui::InputInt("Seed bruit", &seed))
			{
				params.noiseSeed = static_cast<uint32_t>(std::max(0, seed));
			}
			ImGui::SliderFloat("Fréq. bruit (1/m)",
				&params.noiseFrequency, 0.0005f, 0.05f, "%.4f");

			ImGui::Separator();

			// Sélection vertex actif. Si la polyline est vide, on saute le bloc.
			if (!params.vertices.empty())
			{
				size_t activeIdx = tool.GetActiveVertex();
				if (activeIdx >= params.vertices.size())
				{
					activeIdx = params.vertices.size() - 1u;
				}
				int activeI = static_cast<int>(activeIdx);
				int vcount  = static_cast<int>(params.vertices.size());
				if (ImGui::SliderInt("Vertex actif", &activeI, 0, vcount - 1))
				{
					tool.SetActiveVertex(static_cast<size_t>(
						std::clamp(activeI, 0, vcount - 1)));
				}

				auto& v = params.vertices[tool.GetActiveVertex()];
				ImGui::Text("Position monde : X=%.1f m   Z=%.1f m",
					static_cast<double>(v.worldX), static_cast<double>(v.worldZ));
				ImGui::SliderFloat("Largeur base (m)", &v.widthMeters,    10.0f, 2000.0f, "%.1f");
				ImGui::SliderFloat("Hauteur crête (m)", &v.heightMeters,  -1000.0f, 1000.0f, "%.1f");
				ImGui::SliderFloat("Bruit crête (m)",   &v.noiseAmplitude, 0.0f, 200.0f, "%.1f");
				ImGui::SliderFloat("Asymétrie",         &v.asymmetry,     -1.0f, 1.0f, "%.2f");
				if (ImGui::Button("Supprimer vertex"))
				{
					tool.RemoveVertex(tool.GetActiveVertex());
				}
				ImGui::SameLine();
				if (ImGui::Button("Réinitialiser vertex"))
				{
					// Reset partiel : conserve la position, restaure les defaults.
					v.widthMeters    = 250.0f;
					v.heightMeters   = 400.0f;
					v.noiseAmplitude = 30.0f;
					v.asymmetry      = 0.0f;
				}
			}
			else
			{
				ImGui::TextDisabled("Posez des vertices via le canvas top-down ci-dessous.");
			}

			ImGui::Separator();

			// Petit canvas top-down 2D pour poser/déplacer les vertices.
			// État partagé entre Mountain et Valley (mais pas avec Lake/River,
			// pour éviter qu'une bascule d'outil ne perde la vue caméra de
			// l'autre).
			static float canvasHalfM   = 1000.0f;
			static float canvasCenterX = 0.0f;
			static float canvasCenterZ = 0.0f;

			ImGui::SliderFloat("Vue canvas (demi-côté m)", &canvasHalfM,
				50.0f, 10000.0f, "%.0f");
			ImGui::SameLine();
			if (ImGui::Button("Centrer sur polyline"))
			{
				if (!params.vertices.empty())
				{
					float meanX = 0.0f, meanZ = 0.0f;
					for (const auto& vt : params.vertices)
					{
						meanX += vt.worldX;
						meanZ += vt.worldZ;
					}
					canvasCenterX = meanX / static_cast<float>(params.vertices.size());
					canvasCenterZ = meanZ / static_cast<float>(params.vertices.size());
				}
			}

			const ImVec2 canvasSize{ 320.0f, 320.0f };
			ImGui::BeginChild("##macroCanvas", canvasSize, true,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			const ImVec2 contentSize = ImGui::GetContentRegionAvail();
			const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
			const float cw = contentSize.x;
			const float ch = contentSize.y;
			ImGui::InvisibleButton("##macroInteract", contentSize);
			const bool hoveredC = ImGui::IsItemHovered();
			auto pixelToWorld = [&](float px, float py, float& wx, float& wz) {
				const float fx = (px / cw) * 2.0f - 1.0f;
				const float fy = (py / ch) * 2.0f - 1.0f;
				wx = canvasCenterX + fx * canvasHalfM;
				wz = canvasCenterZ - fy * canvasHalfM;
			};
			auto worldToPixel = [&](float wx, float wz, float& px, float& py) {
				const float fx = (wx - canvasCenterX) / canvasHalfM;
				const float fy = (canvasCenterZ - wz) / canvasHalfM;
				px = (fx * 0.5f + 0.5f) * cw;
				py = (fy * 0.5f + 0.5f) * ch;
			};
			if (hoveredC)
			{
				const ImVec2 mp = ImGui::GetIO().MousePos;
				const float px = mp.x - canvasMin.x;
				const float py = mp.y - canvasMin.y;
				float wx = 0.0f, wz = 0.0f;
				pixelToWorld(px, py, wx, wz);
				ImGui::SetTooltip("world: (%.1f, %.1f) m", static_cast<double>(wx),
					static_cast<double>(wz));
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					tool.AddVertex(wx, wz);
				}
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					tool.Cancel();
				}
			}

			ImDrawList* dl = ImGui::GetWindowDrawList();
			// Croix au centre monde.
			{
				float cx = 0.0f, cy = 0.0f;
				worldToPixel(0.0f, 0.0f, cx, cy);
				dl->AddLine(
					ImVec2(canvasMin.x + cx - 5, canvasMin.y + cy),
					ImVec2(canvasMin.x + cx + 5, canvasMin.y + cy),
					IM_COL32(255, 255, 255, 80), 1.0f);
				dl->AddLine(
					ImVec2(canvasMin.x + cx, canvasMin.y + cy - 5),
					ImVec2(canvasMin.x + cx, canvasMin.y + cy + 5),
					IM_COL32(255, 255, 255, 80), 1.0f);
			}
			// Segments polyline.
			for (size_t i = 0; i + 1u < params.vertices.size(); ++i)
			{
				const auto& a = params.vertices[i];
				const auto& b = params.vertices[i + 1u];
				float ax, ay, bx, by;
				worldToPixel(a.worldX, a.worldZ, ax, ay);
				worldToPixel(b.worldX, b.worldZ, bx, by);
				dl->AddLine(
					ImVec2(canvasMin.x + ax, canvasMin.y + ay),
					ImVec2(canvasMin.x + bx, canvasMin.y + by),
					IM_COL32(255, 220, 80, 220), 1.5f);
			}
			// Segment de fermeture si Loop.
			if (params.mode == PolylineMode::Loop && params.vertices.size() >= 2)
			{
				const auto& a = params.vertices.back();
				const auto& b = params.vertices.front();
				float ax, ay, bx, by;
				worldToPixel(a.worldX, a.worldZ, ax, ay);
				worldToPixel(b.worldX, b.worldZ, bx, by);
				dl->AddLine(
					ImVec2(canvasMin.x + ax, canvasMin.y + ay),
					ImVec2(canvasMin.x + bx, canvasMin.y + by),
					IM_COL32(255, 200, 80, 150), 1.0f);
			}
			// Vertices : actifs en orange, autres en jaune.
			for (size_t i = 0; i < params.vertices.size(); ++i)
			{
				const auto& v = params.vertices[i];
				float px, py;
				worldToPixel(v.worldX, v.worldZ, px, py);
				const ImU32 c = (i == tool.GetActiveVertex())
					? IM_COL32(255, 140, 50, 255)
					: IM_COL32(255, 220, 80, 255);
				dl->AddCircleFilled(
					ImVec2(canvasMin.x + px, canvasMin.y + py), 4.0f, c);
			}
			ImGui::EndChild();

			ImGui::Separator();

			const size_t chunkCount = tool.BuildDeltas().size();
			ImGui::Text("Chunks impactés (estimation) : %zu", chunkCount);
			ImGui::Separator();

			const bool canApply = params.vertices.size() >= 2u;
			ImGui::BeginDisabled(!canApply);
			if (ImGui::Button("Apply"))
			{
				tool.Apply();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				tool.Cancel();
			}
		}
#endif
	}

	void ToolPropertiesPanel::RenderMountainRangeParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::MountainRangeTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		RenderMacroPolylineBlock(tool, "Mountain Range — M100.35");
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderValleyChainParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::ValleyChainTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		RenderMacroPolylineBlock(tool, "Valley Chain — M100.35");
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderCoastlineParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::CoastlineEditorTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		auto& ocean = tool.MutableOceanBuffer();

		ImGui::Text("Coastline — M100.37");
		ImGui::Separator();
		ImGui::TextUnformatted("Sea level + ocean parameters (commités à Apply) :");
		bool changed = false;
		changed |= ImGui::SliderFloat("Sea level (Y)", &ocean.seaLevelMeters,
			-100.0f, 500.0f, "%.1f m");
		changed |= ImGui::ColorEdit3("Couleur de fond océan", ocean.bottomColor);
		changed |= ImGui::SliderFloat("Turbidité",
			&ocean.turbidity, 0.0f, 1.0f, "%.2f");
		changed |= ImGui::SliderFloat("Wind influence",
			&ocean.windInfluence, 0.0f, 1.0f, "%.2f");
		changed |= ImGui::Checkbox("Océan activé", &ocean.enabled);
		ImGui::TextDisabled("(source de vérité : WaterDocument::OceanSettings, partagée avec River Network M100.36)");

		ImGui::Separator();
		ImGui::TextUnformatted("Smoothing côtier :");
		ImGui::Checkbox("Lisser la côte", &tool.SmoothingEnabled());
		if (tool.SmoothingEnabled())
		{
			changed |= ImGui::SliderFloat("Bande verticale (m)",
				&tool.SmoothingBandMeters(), 0.5f, 50.0f, "%.1f");
			changed |= ImGui::SliderFloat("Force lissage",
				&tool.SmoothingForce(), 0.0f, 1.0f, "%.2f");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Falaises côtières :");
		ImGui::Checkbox("Générer des falaises", &tool.CliffsEnabled());
		if (tool.CliffsEnabled())
		{
			changed |= ImGui::SliderFloat("Bande verticale (m)##cliffs",
				&tool.CliffsThresholdMeters(), 0.5f, 50.0f, "%.1f");
			changed |= ImGui::SliderFloat("Seuil de pente (deg)",
				&tool.CliffsSlopeThresholdDeg(), 0.0f, 90.0f, "%.1f");
			changed |= ImGui::SliderFloat("Hauteur côté terre (m)",
				&tool.CliffsLandSideMeters(), 0.0f, 30.0f, "%.1f");
			changed |= ImGui::SliderFloat("Profondeur côté mer (m)",
				&tool.CliffsSeaSideMeters(), 0.0f, 30.0f, "%.1f");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Plage automatique :");
		ImGui::Checkbox("Peindre une bande de sable", &tool.BeachSplatEnabled());
		if (tool.BeachSplatEnabled())
		{
			changed |= ImGui::SliderFloat("Largeur côté terre (m)",
				&tool.BeachLandBandMeters(), 0.5f, 50.0f, "%.1f");
			changed |= ImGui::SliderFloat("Largeur côté mer (m)",
				&tool.BeachSeaBandMeters(), 0.5f, 50.0f, "%.1f");
			ImGui::TextDisabled("Note MVP M100.37 : flag conservé, splat sable non câblé à la commande pour l'instant (suivra un follow-up).");
		}

		ImGui::Separator();
		if (changed) tool.RefreshPreview();
		if (ImGui::SmallButton("Rafraîchir preview")) tool.RefreshPreview();

		const auto& stats = tool.Stats();
		ImGui::Text("Terre %u cells · Océan %u cells",
			stats.landCells, stats.oceanCells);
		ImGui::Text("Longueur côte %.1f m (%.2f km)",
			static_cast<double>(stats.coastlineLengthMeters),
			static_cast<double>(stats.coastlineLengthMeters / 1000.0f));
		ImGui::Text("Cellules dans la bande plage : %u", stats.beachBandCells);
		ImGui::Text("Segments marching squares : %zu", tool.Segments().size());

		ImGui::Separator();
		if (ImGui::Button("Apply"))
		{
			tool.Apply();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			tool.Cancel();
		}
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderCaveParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::volumes::caves::CaveTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		const bool advanced =
			engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
				== engine::editor::world::modes::EditorMode::Advanced;
		ImGui::Text("Cave Tool — M100.40 (Phase 11 démarrage)");
		ImGui::TextDisabled("(MVP éditeur-side : rendu glTF runtime à câbler en follow-up)");
		ImGui::Separator();

		const auto& cat = tool.Catalog();
		ImGui::Text("Catalogue : %zu grottes disponibles", cat.Size());
		if (cat.Size() == 0u)
		{
			ImGui::TextDisabled("Aucun catalogue chargé. Vérifie game/data/meshes/caves/catalog.json");
		}
		else
		{
			ImGui::TextUnformatted("Sélection :");
			for (const auto& entry : cat.Entries())
			{
				const bool selected = (tool.SelectedId() == entry.id);
				if (ImGui::RadioButton(entry.displayName.c_str(), selected))
				{
					tool.SelectById(entry.id);
				}
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Position cible (raycast viewport — éditable ici) :");
		ImGui::InputFloat("World X", &tool.TargetWorldX(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("World Z", &tool.TargetWorldZ(), 1.0f, 10.0f, "%.1f");
		float y = tool.TargetWorldY();
		if (ImGui::InputFloat("World Y (terrain)", &y, 1.0f, 10.0f, "%.1f"))
		{
			tool.SetTargetWorldY(y);
		}

		ImGui::Separator();
		ImGui::SliderFloat("Rotation Y (deg)", &tool.RotationYDeg(), -180.0f, 180.0f, "%.1f");
		ImGui::SliderFloat("Scale uniforme",  &tool.UniformScale(),  0.1f, 5.0f, "%.2f");
		ImGui::Checkbox("Snap au sol", &tool.SnapToGround());

		// M100.45 Phase B — mode Advanced : camouflage + flags gameplay.
		if (advanced)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Camouflage entrée :");
			ImGui::Checkbox("Auto-peindre splat « Rocher »", &tool.CamouflageEnabled());
			if (tool.CamouflageEnabled())
			{
				ImGui::SliderFloat("Rayon (m)",   &tool.CamouflageRadius(),   1.0f, 50.0f, "%.1f");
				ImGui::SliderFloat("Force",       &tool.CamouflageStrength(), 0.0f, 1.0f, "%.2f");
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Gameplay :");
			ImGui::Checkbox("Volume intérieur (SurfaceQuery)", &tool.HasInteriorVolume());
			ImGui::Checkbox("Reverb audio",                    &tool.ReceivesAudioReverb());
			ImGui::Checkbox("Permet l'eau (ingress)",          &tool.AllowsWaterIngress());
			ImGui::SliderFloat("Intensité probe lumière",      &tool.LightProbeIntensity(),
				0.0f, 2.0f, "%.2f");
		}
		else
		{
			ImGui::Separator();
			ImGui::TextDisabled("Mode Simple — Options > Mode editeur > Avance pour camouflage + gameplay.");
		}

		ImGui::Separator();
		const bool canPlace = !tool.SelectedId().empty();
		ImGui::BeginDisabled(!canPlace);
		if (ImGui::Button("Place"))
		{
			tool.Place();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			tool.Cancel();
		}

		ImGui::Separator();
		ImGui::Text("Grottes posées : %zu",
			shell.GetMeshInsertDocument().GetByCategory("cave").size());
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderOverhangParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::volumes::overhangs::OverhangTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		const bool advanced =
			engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
				== engine::editor::world::modes::EditorMode::Advanced;
		ImGui::Text("Overhang Tool — M100.41 (Phase 11)");
		ImGui::TextDisabled("(MVP éditeur-side : raycast cliff + normal auto en follow-up M100.17)");
		ImGui::Separator();

		const auto& cat = tool.Catalog();
		ImGui::Text("Catalogue : %zu surplombs disponibles", cat.Size());
		if (cat.Size() == 0u)
		{
			ImGui::TextDisabled("Aucun catalogue chargé. Vérifie game/data/meshes/overhangs/catalog.json");
		}
		else
		{
			ImGui::TextUnformatted("Sélection :");
			for (const auto& entry : cat.Entries())
			{
				const bool selected = (tool.SelectedId() == entry.id);
				if (ImGui::RadioButton(entry.displayName.c_str(), selected))
				{
					tool.SelectById(entry.id);
				}
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Position cible (cliff click → raycast viewport, MVP : input manuel) :");
		ImGui::InputFloat("World X", &tool.TargetWorldX(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("World Y", &tool.TargetWorldY(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("World Z", &tool.TargetWorldZ(), 1.0f, 10.0f, "%.1f");

		ImGui::Separator();
		ImGui::SliderFloat("Yaw normal mur (deg)", &tool.WallNormalYawDeg(), -180.0f, 180.0f, "%.1f");
		ImGui::SliderFloat("Tilt latéral (deg)",   &tool.TiltDeg(),          -30.0f, 30.0f, "%.1f");
		ImGui::SliderFloat("Scale uniforme",       &tool.UniformScale(),     0.1f, 5.0f, "%.2f");

		ImGui::Separator();
		ImGui::TextUnformatted("Validation cliff :");
		ImGui::SliderFloat("Slope requise (deg)",  &tool.RequiredSlopeDeg(), 0.0f, 89.0f, "%.1f");
		ImGui::SliderFloat("Slope observée (deg)", &tool.ObservedSlopeDeg(), 0.0f, 89.0f, "%.1f");
		const bool slopeOk = tool.IsSlopeOk();
		ImGui::TextColored(slopeOk ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
			slopeOk ? "Cliff OK" : "Slope insuffisante");

		// M100.45 Phase B — mode Advanced : flags gameplay + lighting.
		if (advanced)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Gameplay :");
			ImGui::Checkbox("Projette une ombre",          &tool.CastsShadow());
			ImGui::Checkbox("Reverb audio",                &tool.ReceivesAudioReverb());
			ImGui::SliderFloat("Intensité probe lumière",  &tool.LightProbeIntensity(),
				0.0f, 2.0f, "%.2f");
		}
		else
		{
			ImGui::Separator();
			ImGui::TextDisabled("Mode Simple — Options > Mode editeur > Avance pour le gameplay.");
		}

		ImGui::Separator();
		const bool canPlace = !tool.SelectedId().empty() && slopeOk;
		ImGui::BeginDisabled(!canPlace);
		if (ImGui::Button("Place"))
		{
			tool.Place();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			tool.Cancel();
		}

		ImGui::Separator();
		ImGui::Text("Overhangs posés : %zu",
			shell.GetMeshInsertDocument().GetByCategory("overhang").size());
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderArchParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::volumes::arches::ArchTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		const bool advanced =
			engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
				== engine::editor::world::modes::EditorMode::Advanced;
		ImGui::Text("Arch Tool — M100.42 (Phase 11)");
		ImGui::TextDisabled("(MVP éditeur-side : raycast viewport ↦ M100.17)");
		ImGui::Separator();

		const auto& cat = tool.Catalog();
		ImGui::Text("Catalogue : %zu arches disponibles", cat.Size());
		if (cat.Size() == 0u)
		{
			ImGui::TextDisabled("Aucun catalogue chargé. Vérifie game/data/meshes/arches/catalog.json");
		}
		else
		{
			ImGui::TextUnformatted("Sélection :");
			for (const auto& entry : cat.Entries())
			{
				const bool selected = (tool.SelectedId() == entry.id);
				if (ImGui::RadioButton(entry.displayName.c_str(), selected))
				{
					tool.SelectById(entry.id);
				}
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Pied A (monde) :");
		ImGui::InputFloat("A.x", &tool.PointAX(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("A.y", &tool.PointAY(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("A.z", &tool.PointAZ(), 1.0f, 10.0f, "%.1f");

		ImGui::TextUnformatted("Pied B (monde) :");
		ImGui::InputFloat("B.x", &tool.PointBX(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("B.y", &tool.PointBY(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("B.z", &tool.PointBZ(), 1.0f, 10.0f, "%.1f");

		ImGui::Separator();
		ImGui::Text("Span monde     : %.2f m", tool.SpanMeters());
		ImGui::Text("Yaw dérivé     : %.1f deg", tool.DerivedYawDeg());
		const float scale = tool.DerivedScale();
		ImGui::Text("Scale dérivé   : %.2f×", scale);
		const bool scaleOk = (scale >= tool.MinScaleRatio() && scale <= tool.MaxScaleRatio());
		ImGui::TextColored(scaleOk ? ImVec4(0.5f, 1.0f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
			scaleOk ? "Scale dans bornes" : "Scale hors bornes — refusé");

		ImGui::SliderFloat("Min scale", &tool.MinScaleRatio(), 0.05f, 1.0f, "%.2f");
		ImGui::SliderFloat("Max scale", &tool.MaxScaleRatio(), 1.0f, 10.0f, "%.2f");

		// M100.45 Phase B — mode Advanced : flags gameplay + lighting.
		if (advanced)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Gameplay :");
			ImGui::Checkbox("Projette une ombre", &tool.CastsShadow());
			ImGui::SliderFloat("Intensité probe lumière", &tool.LightProbeIntensity(),
				0.0f, 2.0f, "%.2f");
		}
		else
		{
			ImGui::Separator();
			ImGui::TextDisabled("Mode Simple — Options > Mode editeur > Avance pour le gameplay.");
		}

		ImGui::Separator();
		const bool canPlace = !tool.SelectedId().empty() && scaleOk;
		ImGui::BeginDisabled(!canPlace);
		if (ImGui::Button("Place"))
		{
			tool.Place();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			tool.Cancel();
		}

		ImGui::Separator();
		ImGui::Text("Arches posées : %zu",
			shell.GetMeshInsertDocument().GetByCategory("arch").size());
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderDungeonPortalParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::volumes::dungeons::DungeonPortalTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		const bool advanced =
			engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
				== engine::editor::world::modes::EditorMode::Advanced;
		ImGui::Text("Dungeon Portal Tool — M100.43 (Phase 11)");
		ImGui::TextDisabled("(MVP éditeur-side : handler serveur câblé en M100.44)");
		ImGui::Separator();

		const auto& cat = tool.Catalog();
		ImGui::Text("Catalogue : %zu templates de donjon", cat.Size());
		if (cat.Size() == 0u)
		{
			ImGui::TextDisabled("Aucun catalogue. Vérifie game/data/meshes/dungeons/catalog.json");
		}
		else
		{
			ImGui::TextUnformatted("Sélection :");
			for (const auto& entry : cat.Entries())
			{
				const bool selected = (tool.SelectedTemplateId() == entry.id);
				if (ImGui::RadioButton(entry.displayName.c_str(), selected))
				{
					tool.SelectByTemplateId(entry.id);
				}
				if (selected && !entry.description.empty())
				{
					ImGui::TextWrapped("%s", entry.description.c_str());
				}
			}
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Position cible :");
		ImGui::InputFloat("World X", &tool.TargetWorldX(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("World Y", &tool.TargetWorldY(), 1.0f, 10.0f, "%.1f");
		ImGui::InputFloat("World Z", &tool.TargetWorldZ(), 1.0f, 10.0f, "%.1f");
		ImGui::SliderFloat("Yaw (deg)", &tool.YawDeg(), -180.0f, 180.0f, "%.1f");
		ImGui::SliderFloat("Trigger radius (m)", &tool.TriggerRadius(), 0.5f, 30.0f, "%.1f");

		// M100.45 Phase B — mode Advanced : gating gameplay détaillé.
		// En mode Simple, niveau/difficulté gardent les valeurs préremplies
		// depuis le catalogue par SelectByTemplateId (Place reste valide).
		if (advanced)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Gating gameplay :");
			int reqLevel = static_cast<int>(tool.RequiredLevel());
			if (ImGui::SliderInt("Niveau requis", &reqLevel, 1, 80))
				tool.RequiredLevel() = static_cast<uint16_t>(reqLevel);
			int minD = static_cast<int>(tool.MinDifficulty());
			int maxD = static_cast<int>(tool.MaxDifficulty());
			if (ImGui::SliderInt("Min difficulty", &minD, 1, 5))
				tool.MinDifficulty() = static_cast<uint8_t>(minD);
			if (ImGui::SliderInt("Max difficulty", &maxD, 1, 5))
				tool.MaxDifficulty() = static_cast<uint8_t>(maxD);
			ImGui::Checkbox("One-shot (raid partagé)",        &tool.IsOneShot());
			ImGui::Checkbox("Persiste entre les sessions",    &tool.PersistsAcrossLogin());
		}
		else
		{
			ImGui::Separator();
			ImGui::TextDisabled("Mode Simple — gating niveau/difficulté hérité du catalogue.");
		}

		ImGui::Separator();
		const bool canPlace = !tool.SelectedTemplateId().empty()
			&& tool.MinDifficulty() > 0u
			&& tool.MaxDifficulty() >= tool.MinDifficulty();
		ImGui::BeginDisabled(!canPlace);
		if (ImGui::Button("Place"))
		{
			tool.Place();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			tool.Cancel();
		}

		ImGui::Separator();
		ImGui::Text("Portails posés : %zu",
			shell.GetDungeonPortalDocument().Size());

		// --- M100.44 : VMap Bridge & Phase 11 Validation (clôture) ---
		ImGui::Separator();
		ImGui::TextUnformatted("VMap Bridge & validation (Phase 11) :");

		namespace vb = engine::editor::world::volumes::bridge;
		const auto& meshDoc   = shell.GetMeshInsertDocument();
		const auto& portalDoc = shell.GetDungeonPortalDocument();

		// Validation cohérence.
		vb::Phase11Validator validator;
		validator.SetCaveCatalog(&shell.GetCaveTool().Catalog());
		validator.SetOverhangCatalog(&shell.GetOverhangTool().Catalog());
		validator.SetArchCatalog(&shell.GetArchTool().Catalog());
		validator.SetDungeonCatalog(&shell.GetDungeonPortalTool().Catalog());
		const vb::ValidationReport report = validator.Validate(meshDoc, portalDoc);

		ImGui::Text("Validation : %zu erreur(s), %zu avertissement(s), %zu info",
			report.errorCount, report.warningCount, report.infoCount);
		for (const auto& issue : report.issues)
		{
			ImVec4 col;
			switch (issue.severity)
			{
				case vb::ValidationSeverity::Error:
					col = ImVec4(1.0f, 0.45f, 0.45f, 1.0f); break;
				case vb::ValidationSeverity::Warning:
					col = ImVec4(1.0f, 0.82f, 0.40f, 1.0f); break;
				default:
					col = ImVec4(0.65f, 0.78f, 1.0f, 1.0f); break;
			}
			ImGui::TextColored(col, "  - %s", issue.message.c_str());
		}

		// Export VMap : gardé derrière l'absence d'erreurs bloquantes.
		const bool exportBlocked = report.HasBlockingErrors();
		ImGui::BeginDisabled(exportBlocked);
		if (ImGui::Button("Exporter collision VMap (volume_collision.bin)"))
		{
			vb::VMapBridge bridge;
			bridge.SetCaveCatalog(&shell.GetCaveTool().Catalog());
			bridge.SetOverhangCatalog(&shell.GetOverhangTool().Catalog());
			bridge.SetArchCatalog(&shell.GetArchTool().Catalog());
			size_t unresolved = 0u;
			bridge.Build(meshDoc, portalDoc, unresolved);
			std::string err;
			const std::string contentRoot = "game/data";
			if (bridge.WriteToDisk(contentRoot, err))
			{
				LOG_INFO(EditorWorld,
					"[ToolPropertiesPanel] VMap export OK : {} proxies ({} non résolus)",
					bridge.Size(), unresolved);
			}
			else
			{
				LOG_WARN(EditorWorld, "[ToolPropertiesPanel] VMap export échec : {}", err);
			}
		}
		ImGui::EndDisabled();
		if (exportBlocked)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
				"Export bloqué : corrige les erreurs ci-dessus.");
		}

		ImGui::TextDisabled("Opcode 197/198 câblés (EnterDungeonHandler) — M100.44");
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderThermalWindErosionParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::erosion::ThermalWindErosionTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		auto& p = tool.MutableParams();
		// M100.45 Phase B — outil migré Simple/Advanced.
		const bool advanced =
			engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
				== engine::editor::world::modes::EditorMode::Advanced;

		ImGui::Text("Thermal / Wind Erosion — M100.39");
		ImGui::TextDisabled("(clôt la Phase 2.5 — Terrain naturaliste)");
		ImGui::Separator();

		// M100.45 A.6 — dropdown de presets (tool_presets/thermal_wind_erosion.json).
		engine::editor::world::ui::RenderPresetDropdown(
			"thermal_wind_erosion",
			[&p](const engine::editor::world::presets::ToolPreset& preset) {
				engine::editor::world::presets::ApplyThermalWindErosionPreset(p, preset);
			});

		// Sous-mode.
		int subModeIdx = static_cast<int>(p.subMode);
		static const char* kSubModeLabels[3] = {
			"Thermal seul (~500 ms)",
			"Wind seul (~5-15 s)",
			"Both (Thermal puis Wind)",
		};
		ImGui::Combo("Sous-mode", &subModeIdx, kSubModeLabels, 3);
		p.subMode = static_cast<engine::editor::world::erosion::ErosionSubMode>(
			std::clamp(subModeIdx, 0, 2));

		const bool showThermal = (p.subMode == engine::editor::world::erosion::ErosionSubMode::Thermal
			|| p.subMode == engine::editor::world::erosion::ErosionSubMode::Both);
		const bool showWind = (p.subMode == engine::editor::world::erosion::ErosionSubMode::Wind
			|| p.subMode == engine::editor::world::erosion::ErosionSubMode::Both);

		if (showThermal)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Thermal Erosion :");
			auto& t = p.thermal;
			// Mode Simple : la « force globale » suffit.
			ImGui::SliderFloat("Force par passe",      &t.forcePerPass, 0.0f, 1.0f, "%.2f");
			if (advanced)
			{
				ImGui::SliderFloat("Angle de talus (deg)", &t.talusAngleDeg, 5.0f, 80.0f, "%.1f");
				int np = static_cast<int>(t.numPasses);
				if (ImGui::SliderInt("Nombre de passes",   &np, 1, 200))
				{
					t.numPasses = static_cast<uint32_t>(std::max(1, np));
				}
				ImGui::SliderFloat("Pente min activation (deg)",
					&t.minActivationSlopeDeg, 0.0f, 45.0f, "%.1f");
				ImGui::Checkbox("Stopper sous sea level##th",  &t.stopUnderSeaLevel);
				ImGui::Checkbox("Préserver pentes raides",     &t.preserveSteepSlopes);
				if (t.preserveSteepSlopes)
				{
					ImGui::SliderFloat("Seuil exception (deg)",
						&t.preserveSteepThresholdDeg, 0.0f, 90.0f, "%.1f");
				}
			}
		}

		if (showWind)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Wind Erosion :");
			auto& w = p.wind;
			// Mode Simple : la « force du vent » suffit.
			ImGui::SliderFloat("Force du vent",           &w.windStrength, 0.0f, 2.0f, "%.2f");
			if (advanced)
			{
				ImGui::SliderFloat("Direction du vent (deg)", &w.windAngleDeg, 0.0f, 360.0f, "%.1f");
				int npart = static_cast<int>(w.numParticles);
				if (ImGui::SliderInt("Nombre de particules", &npart, 0, 200000))
				{
					w.numParticles = static_cast<uint32_t>(std::max(0, npart));
				}
				int life = static_cast<int>(w.maxLifetimeSteps);
				if (ImGui::SliderInt("Durée de vie max",     &life, 1, 200))
				{
					w.maxLifetimeSteps = static_cast<uint32_t>(std::max(1, life));
				}
				ImGui::SliderFloat("Capacité sable",         &w.sandCapacityFactor, 0.0f, 2.0f, "%.2f");
				ImGui::SliderFloat("Exposition R (m)",       &w.exposureRadiusMeters, 1.0f, 200.0f, "%.1f");
				int seed = static_cast<int>(w.rngSeed);
				if (ImGui::InputInt("Seed RNG##wind", &seed))
				{
					w.rngSeed = static_cast<uint32_t>(std::max(0, seed));
				}
				ImGui::Checkbox("Stopper sous sea level##wd", &w.stopUnderSeaLevel);
				ImGui::Checkbox("Restreindre aux cellules Sand (flag MVP)", &w.restrictToSandSplat);
				if (w.restrictToSandSplat)
				{
					ImGui::TextDisabled("Note MVP : flag conservé mais non câblé au splat (follow-up).");
				}
			}
		}

		if (!advanced)
		{
			ImGui::Separator();
			ImGui::TextDisabled("Mode Simple — Options > Mode editeur > Avance pour tout regler.");
		}

		ImGui::Separator();
		ImGui::TextUnformatted("Workflow recommandé :");
		ImGui::TextDisabled("M100.35 mountains -> 36 rivers -> 37 coast -> 38 hydraulic -> 39 thermal+wind");
		ImGui::Separator();

		if (ImGui::Button("▶ Simulate"))
		{
			tool.Simulate();
		}

		if (tool.HasResult())
		{
			ImGui::Separator();
			const auto& tr = tool.LastThermalResult();
			const auto& wr = tool.LastWindResult();
			ImGui::Text("Résultat dernière simulation :");
			if (showThermal)
			{
				ImGui::Text("  Thermal passes      : %u%s",
					tr.passesExecuted,
					tr.converged ? " (convergé)" : "");
				ImGui::Text("  Total transféré (T) : %.2f m", static_cast<double>(tr.totalTransferredMeters));
				ImGui::Text("  Temps (T)           : %.1f ms", tr.wallTimeMillis);
			}
			if (showWind)
			{
				ImGui::Text("  Particules simulées : %u", wr.particlesSimulated);
				ImGui::Text("  Total steps (W)     : %llu",
					static_cast<unsigned long long>(wr.totalSteps));
				ImGui::Text("  Érodées / Déposées  : %u / %u",
					wr.cellsEroded, wr.cellsDeposited);
				ImGui::Text("  Δ min / max (W)     : %.2f / %.2f m",
					static_cast<double>(wr.minDelta),
					static_cast<double>(wr.maxDelta));
				ImGui::Text("  Temps (W)           : %.1f ms", wr.wallTimeMillis);
			}

			ImGui::Separator();
			if (ImGui::Button("Apply"))   tool.Apply();
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))  tool.Cancel();
			ImGui::SameLine();
			if (ImGui::Button("Re-simulate")) tool.Simulate();
		}
		else
		{
			ImGui::TextDisabled("Cliquez Simulate pour lancer l'érosion choisie.");
		}
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderHydraulicErosionParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::erosion::HydraulicErosionTool& tool)
	{
#if defined(_WIN32)
		(void)shell;
		auto& p = tool.MutableParams();
		// M100.45 Phase B — outil migré Simple/Advanced. Le mode courant
		// vient du EditorModeRegistry (toggle Options > Mode editeur).
		const bool advanced =
			engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
				== engine::editor::world::modes::EditorMode::Advanced;

		ImGui::Text("Hydraulic Erosion — M100.38");
		ImGui::TextDisabled("(simulation particle-based, lit sea level via OceanSettings)");
		ImGui::Separator();

		// M100.45 A.6 — dropdown de presets. La sélection applique les
		// valeurs JSON (tool_presets/hydraulic_erosion.json) au struct.
		engine::editor::world::ui::RenderPresetDropdown(
			"hydraulic_erosion",
			[&p](const engine::editor::world::presets::ToolPreset& preset) {
				engine::editor::world::presets::ApplyHydraulicErosionPreset(p, preset);
			});

		// --- Mode Simple : 3 paramètres essentiels, toujours visibles ---
		int n = static_cast<int>(p.numDroplets);
		if (ImGui::SliderInt("Nombre de gouttes", &n, 0, 500000))
		{
			p.numDroplets = static_cast<uint32_t>(std::max(0, n));
		}
		ImGui::SliderFloat("Intensité (taux d'érosion)", &p.erosionRate, 0.0f, 1.0f, "%.2f");
		int life = static_cast<int>(p.maxLifetimeSteps);
		if (ImGui::SliderInt("Niveau de détail (durée de vie)", &life, 1, 200))
		{
			p.maxLifetimeSteps = static_cast<uint32_t>(std::max(1, life));
		}

		// --- Mode Advanced : tous les paramètres physiques + bornes ---
		if (advanced)
		{
			ImGui::Separator();
			ImGui::TextUnformatted("Physique :");
			ImGui::SliderFloat("Capacité sédiment",  &p.sedimentCapacity,  0.1f, 20.0f, "%.2f");
			ImGui::SliderFloat("Taux de déposition", &p.depositionRate,    0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Évaporation",        &p.evaporationRate,   0.0f, 0.5f, "%.3f");
			ImGui::SliderFloat("Gravité",            &p.gravity,           0.1f, 20.0f, "%.2f");
			ImGui::SliderFloat("Inertie",            &p.inertia,           0.0f, 1.0f, "%.2f");
			ImGui::SliderFloat("Pente min érosion",  &p.minSlopeForErosion, 0.0f, 1.0f, "%.3f");
			ImGui::SliderFloat("Delta max / cell (m)", &p.maxDeltaPerCellMeters, 0.1f, 20.0f, "%.2f");

			ImGui::Separator();
			ImGui::TextUnformatted("Initialisation :");
			int distIdx = static_cast<int>(p.distribution);
			static const char* kDistLabels[3] = { "Uniform", "Weighted altitude", "Weighted flow accum" };
			if (ImGui::Combo("Distribution sources", &distIdx, kDistLabels, 3))
			{
				p.distribution = static_cast<engine::editor::world::erosion::DropletDistribution>(
					std::clamp(distIdx, 0, 2));
			}
			int seed = static_cast<int>(p.rngSeed);
			if (ImGui::InputInt("Seed RNG", &seed))
			{
				p.rngSeed = static_cast<uint32_t>(std::max(0, seed));
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Bornes / sécurité :");
			ImGui::Checkbox("Stopper sous sea level", &p.stopUnderSeaLevel);
			ImGui::Checkbox("Préserver les zones plates", &p.preserveFlatAreas);
			if (p.preserveFlatAreas)
			{
				ImGui::SliderFloat("Seuil pente plate (deg)",
					&p.flatAreaSlopeThresholdDeg, 0.0f, 45.0f, "%.1f");
			}
		}
		else
		{
			ImGui::TextDisabled("Mode Simple — Options > Mode editeur > Avance pour tout regler.");
		}

		ImGui::Separator();
		if (ImGui::Button("▶ Simulate"))
		{
			tool.Simulate();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(~1-20 s bloquant selon numDroplets)");

		if (tool.HasResult())
		{
			const auto& r = tool.LastResult();
			ImGui::Separator();
			ImGui::Text("Résultat dernière simulation :");
			ImGui::Text("  Gouttes simulées : %u", r.dropletsSimulated);
			ImGui::Text("  Total steps      : %llu",
				static_cast<unsigned long long>(r.totalSteps));
			ImGui::Text("  Cellules érodées : %u", r.cellsEroded);
			ImGui::Text("  Cellules déposées: %u", r.cellsDeposited);
			ImGui::Text("  Delta min / max  : %.2f / %.2f m",
				static_cast<double>(r.minDelta), static_cast<double>(r.maxDelta));
			ImGui::Text("  Temps total      : %.1f ms", r.wallTimeMillis);
			ImGui::Checkbox("Afficher preview érosion (overlay 2D)",
				&tool.PreviewEnabled());

			ImGui::Separator();
			if (ImGui::Button("Apply"))
			{
				tool.Apply();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				tool.Cancel();
			}
			ImGui::SameLine();
			if (ImGui::Button("Re-simulate"))
			{
				tool.Simulate();
			}
		}
		else
		{
			ImGui::TextDisabled("Cliquez Simulate pour lancer une passe d'érosion.");
		}
#else
		(void)shell; (void)tool;
#endif
	}

	void ToolPropertiesPanel::RenderRiverNetworkParams(
		engine::editor::world::WorldEditorShell& shell,
		engine::editor::world::RiverNetworkTool& tool)
	{
#if defined(_WIN32)
		// M100.45 Phase B — outil migré Simple/Advanced.
		const bool advanced =
			engine::editor::world::modes::EditorModeRegistry::Instance().GetCurrentMode()
				== engine::editor::world::modes::EditorMode::Advanced;

		ImGui::Text("River Network — M100.36 (watershed D8)");

		// Liste des sources posées.
		ImGui::Text("Sources posées : %zu / %zu",
			tool.Springs().size(),
			engine::editor::world::RiverNetworkTool::kMaxSprings);
		if (ImGui::SmallButton("Reset all sources"))
		{
			tool.Cancel();
		}
		ImGui::Separator();

		// Petit canvas top-down pour poser les sources. Échelle 1000 m de
		// demi-côté pour matcher les distances typiques d'une zone 10 km.
		static float canvasHalfM   = 1000.0f;
		static float canvasCenterX = 0.0f;
		static float canvasCenterZ = 0.0f;
		ImGui::SliderFloat("Vue canvas (demi-côté m)", &canvasHalfM,
			50.0f, 5000.0f, "%.0f");

		const ImVec2 canvasSize{ 320.0f, 320.0f };
		ImGui::BeginChild("##riverNetCanvas", canvasSize, true,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		const ImVec2 contentSize = ImGui::GetContentRegionAvail();
		const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
		const float cw = contentSize.x;
		const float ch = contentSize.y;
		ImGui::InvisibleButton("##rnInteract", contentSize);
		const bool hoveredC = ImGui::IsItemHovered();
		auto pixelToWorld = [&](float px, float py, float& wx, float& wz) {
			const float fx = (px / cw) * 2.0f - 1.0f;
			const float fy = (py / ch) * 2.0f - 1.0f;
			wx = canvasCenterX + fx * canvasHalfM;
			wz = canvasCenterZ - fy * canvasHalfM;
		};
		auto worldToPixel = [&](float wx, float wz, float& px, float& py) {
			const float fx = (wx - canvasCenterX) / canvasHalfM;
			const float fy = (canvasCenterZ - wz) / canvasHalfM;
			px = (fx * 0.5f + 0.5f) * cw;
			py = (fy * 0.5f + 0.5f) * ch;
		};
		if (hoveredC)
		{
			const ImVec2 mp = ImGui::GetIO().MousePos;
			const float px = mp.x - canvasMin.x;
			const float py = mp.y - canvasMin.y;
			float wx = 0.0f, wz = 0.0f;
			pixelToWorld(px, py, wx, wz);
			ImGui::SetTooltip("world: (%.1f, %.1f) m", static_cast<double>(wx),
				static_cast<double>(wz));
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				// Sample Y depuis le doc terrain (chunk déjà résident dans
				// le pire des cas — sinon Y=0 et la sim re-sample de toute façon).
				float worldY = 0.0f;
				auto& terrainDoc = shell.MutableTerrainDocument();
				const int chunkSpan =
					(engine::world::terrain::kTerrainResolution - 1) * 1;
				const int chunkX = static_cast<int>(std::floor(wx / chunkSpan));
				const int chunkZ = static_cast<int>(std::floor(wz / chunkSpan));
				auto chunkPtr = terrainDoc.Find({ chunkX, chunkZ });
				if (chunkPtr)
				{
					const float localX = wx - chunkX * static_cast<float>(chunkSpan);
					const float localZ = wz - chunkZ * static_cast<float>(chunkSpan);
					worldY = chunkPtr->SampleHeight(localX, localZ);
				}
				tool.AddSpring(wx, wz, worldY);
			}
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
			{
				// Cherche la source la plus proche dans un rayon de 30 m et la retire.
				float bestDist = 30.0f;
				size_t bestIdx = tool.Springs().size();
				for (size_t i = 0; i < tool.Springs().size(); ++i)
				{
					const float dx = tool.Springs()[i].worldX - wx;
					const float dz = tool.Springs()[i].worldZ - wz;
					const float d = std::sqrt(dx * dx + dz * dz);
					if (d < bestDist)
					{
						bestDist = d;
						bestIdx = i;
					}
				}
				if (bestIdx < tool.Springs().size())
				{
					tool.RemoveSpring(bestIdx);
				}
			}
		}
		ImDrawList* dl = ImGui::GetWindowDrawList();
		{
			float cx = 0.0f, cy = 0.0f;
			worldToPixel(0.0f, 0.0f, cx, cy);
			dl->AddLine(
				ImVec2(canvasMin.x + cx - 5, canvasMin.y + cy),
				ImVec2(canvasMin.x + cx + 5, canvasMin.y + cy),
				IM_COL32(255, 255, 255, 80), 1.0f);
			dl->AddLine(
				ImVec2(canvasMin.x + cx, canvasMin.y + cy - 5),
				ImVec2(canvasMin.x + cx, canvasMin.y + cy + 5),
				IM_COL32(255, 255, 255, 80), 1.0f);
		}
		for (const auto& s : tool.Springs())
		{
			float px, py;
			worldToPixel(s.worldX, s.worldZ, px, py);
			dl->AddCircleFilled(ImVec2(canvasMin.x + px, canvasMin.y + py),
				5.0f, IM_COL32(80, 180, 255, 255));
		}
		// Preview du résultat dernier Simulate : lignes bleues pour les rivières.
		if (tool.HasResult())
		{
			for (const auto& river : tool.LastResult().rivers)
			{
				for (size_t i = 0; i + 1u < river.nodes.size(); ++i)
				{
					float ax, ay, bx, by;
					worldToPixel(river.nodes[i].position.x, river.nodes[i].position.z, ax, ay);
					worldToPixel(river.nodes[i + 1u].position.x, river.nodes[i + 1u].position.z, bx, by);
					dl->AddLine(
						ImVec2(canvasMin.x + ax, canvasMin.y + ay),
						ImVec2(canvasMin.x + bx, canvasMin.y + by),
						IM_COL32(60, 140, 240, 180), 2.0f);
				}
			}
			// Lacs auto en bleu rempli.
			for (const auto& lake : tool.LastResult().autoLakes)
			{
				for (size_t i = 0; i < lake.polygon.size(); ++i)
				{
					const auto& a = lake.polygon[i];
					const auto& b = lake.polygon[(i + 1u) % lake.polygon.size()];
					float ax, ay, bx, by;
					worldToPixel(a.x, a.z, ax, ay);
					worldToPixel(b.x, b.z, bx, by);
					dl->AddLine(
						ImVec2(canvasMin.x + ax, canvasMin.y + ay),
						ImVec2(canvasMin.x + bx, canvasMin.y + by),
						IM_COL32(60, 100, 220, 220), 1.5f);
				}
			}
		}
		ImGui::EndChild();

		ImGui::Separator();
		ImGui::TextUnformatted("Paramètres de simulation :");

		// M100.45 A.6 — dropdown de presets (tool_presets/river_network.json).
		engine::editor::world::ui::RenderPresetDropdown(
			"river_network",
			[&tool](const engine::editor::world::presets::ToolPreset& preset) {
				engine::editor::world::presets::ApplyRiverNetworkPreset(
					tool.MutableParams(), preset);
			});

		// Sea level — buffer local, écrit dans WaterDocument seulement à Apply.
		float seaBuf = tool.SeaLevelBuffer();
		if (ImGui::SliderFloat("Sea level (Y)", &seaBuf, -100.0f, 500.0f, "%.1f m"))
		{
			tool.SetSeaLevelBuffer(seaBuf);
		}
		ImGui::TextDisabled("(source de vérité : WaterDocument::GetOcean — édité aussi par Coastline M100.37)");

		// Mode Simple : « densité » du réseau = flow threshold.
		int flowThreshold = static_cast<int>(tool.MutableParams().minFlowThresholdCells);
		if (ImGui::SliderInt("Densité (flow threshold, cells)", &flowThreshold, 1, 5000))
		{
			tool.MutableParams().minFlowThresholdCells = static_cast<uint32_t>(std::max(1, flowThreshold));
		}

		if (advanced)
		{
			ImGui::SliderFloat("Tolerance Douglas-P. (m)",
				&tool.MutableParams().simplificationToleranceMeters, 0.5f, 50.0f, "%.1f");
			ImGui::Checkbox("Auto-lakes at sinks", &tool.MutableParams().autoLakesAtSinks);
			if (tool.MutableParams().autoLakesAtSinks)
			{
				ImGui::SliderFloat("Max lake depth (m)",
					&tool.MutableParams().autoLakeMaxDepthMeters, 0.5f, 100.0f, "%.1f");
			}

			ImGui::Separator();
			ImGui::Checkbox("Carve heightmap along rivers", &tool.MutableParams().carveHeightmap);
			if (tool.MutableParams().carveHeightmap)
			{
				ImGui::SliderFloat("Carve depth (m)",
					&tool.MutableParams().carveDepthMeters, 0.1f, 30.0f, "%.1f");
				ImGui::SliderFloat("Carve width (m)",
					&tool.MutableParams().carveWidthMeters, 1.0f, 100.0f, "%.1f");
			}
		}
		else
		{
			ImGui::TextDisabled("Mode Simple — Options > Mode editeur > Avance pour tolérance + lacs + carving.");
		}

		ImGui::Separator();
		if (ImGui::Button("Simulate"))
		{
			tool.Simulate();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("⌛ ~1-5 s sur une zone complète.");

		if (tool.HasResult())
		{
			const auto& r = tool.LastResult();
			ImGui::Text("🌊 %zu rivières · %u confluences · %u embouchures · %zu lacs auto",
				r.rivers.size(), r.confluenceCount, r.mouthCount, r.autoLakes.size());
			if (r.rejectedByThreshold > 0)
			{
				ImGui::TextDisabled("  (rejetées par threshold : %u)", r.rejectedByThreshold);
			}
		}

		ImGui::Separator();
		const bool canApply = tool.HasResult() && !tool.LastResult().rivers.empty();
		ImGui::BeginDisabled(!canApply);
		if (ImGui::Button("Apply"))
		{
			tool.Apply();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			tool.Cancel();
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
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::MountainRange)
			{
				ImGui::TextUnformatted("Mountain Range");
				ImGui::Separator();
				RenderMountainRangeParams(*m_shell, m_shell->MutableMountainRangeTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::ValleyChain)
			{
				ImGui::TextUnformatted("Valley Chain");
				ImGui::Separator();
				RenderValleyChainParams(*m_shell, m_shell->MutableValleyChainTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::RiverNetwork)
			{
				ImGui::TextUnformatted("River Network");
				ImGui::Separator();
				RenderRiverNetworkParams(*m_shell, m_shell->MutableRiverNetworkTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::Coastline)
			{
				ImGui::TextUnformatted("Coastline");
				ImGui::Separator();
				RenderCoastlineParams(*m_shell, m_shell->MutableCoastlineEditorTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::HydraulicErosion)
			{
				ImGui::TextUnformatted("Hydraulic Erosion");
				ImGui::Separator();
				RenderHydraulicErosionParams(*m_shell, m_shell->MutableHydraulicErosionTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::ThermalWindErosion)
			{
				ImGui::TextUnformatted("Thermal / Wind Erosion");
				ImGui::Separator();
				RenderThermalWindErosionParams(*m_shell, m_shell->MutableThermalWindErosionTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::Cave)
			{
				ImGui::TextUnformatted("Cave (Phase 11)");
				ImGui::Separator();
				RenderCaveParams(*m_shell, m_shell->MutableCaveTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::Overhang)
			{
				ImGui::TextUnformatted("Overhang (Phase 11)");
				ImGui::Separator();
				RenderOverhangParams(*m_shell, m_shell->MutableOverhangTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::Arch)
			{
				ImGui::TextUnformatted("Arch (Phase 11)");
				ImGui::Separator();
				RenderArchParams(*m_shell, m_shell->MutableArchTool());
			}
			else if (m_shell != nullptr &&
				m_shell->GetActiveTool() == engine::editor::world::ActiveTool::DungeonPortal)
			{
				ImGui::TextUnformatted("Dungeon Portal (Phase 11)");
				ImGui::Separator();
				RenderDungeonPortalParams(*m_shell, m_shell->MutableDungeonPortalTool());
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
