#include "src/world_editor/zone_presets/OperationDispatcher.h"

#include "src/client/world/WorldModel.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/water/WaterSurfaces.h"
#include "src/shared/core/Log.h"
#include "src/world_editor/presets/ToolPresetApply.h"
#include "src/world_editor/presets/ToolPresetRegistry.h"
#include "src/world_editor/terrain/MountainRangeCommand.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/world_editor/terrain/ValleyChainCommand.h"
#include "src/world_editor/terrain/erosion/HydraulicErosionCommand.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulation.h"
#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/terrain/erosion/ThermalSimulation.h"
#include "src/world_editor/terrain/erosion/ThermalSimulationParams.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionCommand.h"
#include "src/world_editor/terrain/erosion/ThermalWindErosionParams.h"
#include "src/world_editor/terrain/erosion/WindSimulation.h"
#include "src/world_editor/terrain/erosion/WindSimulationParams.h"
#include "src/world_editor/volumes/MeshInsertDocument.h"
#include "src/world_editor/volumes/MeshInsertInstance.h"
#include "src/world_editor/volumes/arches/ArchCatalog.h"
#include "src/world_editor/volumes/arches/PlaceArchCommand.h"
#include "src/world_editor/volumes/caves/CaveCatalog.h"
#include "src/world_editor/volumes/caves/PlaceCaveCommand.h"
#include "src/world_editor/volumes/dungeons/DungeonCatalog.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalDocument.h"
#include "src/world_editor/volumes/dungeons/DungeonPortalInstance.h"
#include "src/world_editor/volumes/dungeons/PlaceDungeonPortalCommand.h"
#include "src/world_editor/volumes/overhangs/OverhangCatalog.h"
#include "src/world_editor/volumes/overhangs/PlaceOverhangCommand.h"
#include "src/world_editor/water/AddLakeCommand.h"
#include "src/world_editor/water/AddRiverCommand.h"
#include "src/world_editor/water/CoastlineCliffs.h"
#include "src/world_editor/water/CoastlineCommand.h"
#include "src/world_editor/water/CoastlineSmoothing.h"
#include "src/world_editor/water/ConsolidatedHeightGrid.h"
#include "src/world_editor/water/HeightGridAssembly.h"
#include "src/world_editor/water/OceanSettings.h"
#include "src/world_editor/water/RiverNetworkCommand.h"
#include "src/world_editor/water/SpringSource.h"
#include "src/world_editor/water/WaterDocument.h"
#include "src/world_editor/water/WatershedSimulation.h"
#include "src/world_editor/water/WatershedSimulationParams.h"
#include "src/world_editor/zone_presets/OperationParams.h"

#include <cmath>
#include <optional>
#include <utility>

namespace engine::editor::world::zone_presets
{
	namespace
	{
		using engine::math::Vec3;

		/// Lit `worldPosition` (3 floats aplatis) depuis `params`.
		/// Fallback : (0,0,0). Retourne false si la clé est absente.
		bool ReadWorldPos(const OperationParams& params, Vec3& out)
		{
			std::vector<double> v;
			if (!params.GetNumberList("worldPosition", v) || v.size() < 3u)
				return false;
			out.x = static_cast<float>(v[0]);
			out.y = static_cast<float>(v[1]);
			out.z = static_cast<float>(v[2]);
			return true;
		}

		float ReadFloat(const OperationParams& params, const char* key, float fallback)
		{
			double v = 0.0;
			return params.GetNumber(key, v) ? static_cast<float>(v) : fallback;
		}

		bool ReadBool(const OperationParams& params, const char* key, bool fallback)
		{
			bool v = false;
			return params.GetBool(key, v) ? v : fallback;
		}

		// --- place_cave ----------------------------------------------------

		DispatchResult DispatchPlaceCave(const OperationParams& params,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			std::string catalogId;
			if (!params.GetString("catalogId", catalogId) || catalogId.empty())
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_cave : catalogId manquant");
				return DispatchResult::Failed;
			}
			const auto* entry = ctx.caveCatalog.FindById(catalogId);
			if (entry == nullptr)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_cave : catalogId '{}' introuvable",
					catalogId);
				return DispatchResult::Failed;
			}
			Vec3 worldPos{};
			if (!ReadWorldPos(params, worldPos))
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_cave : worldPosition manquant");
				return DispatchResult::Failed;
			}
			const float rotationY = ReadFloat(params, "rotationY", 0.0f);
			const bool  snapToGround = ReadBool(params, "autoSnapToGround", true);

			using engine::editor::world::volumes::MeshInsertInstance;
			using engine::editor::world::volumes::caves::PlaceCaveCommand;

			MeshInsertInstance inst;
			inst.guid                = 0u;
			inst.gltfRelativePath    = entry->gltfRelativePath;
			inst.worldPosition       = worldPos;
			if (snapToGround) inst.worldPosition.y -= entry->entrancePoint.y;
			inst.eulerRotationDeg    = { 0.0f, rotationY, 0.0f };
			inst.uniformScale        = ReadFloat(params, "uniformScale", 1.0f);
			inst.insertCategory      = "cave";
			inst.displayName         = entry->displayName.empty() ? entry->id : entry->displayName;
			inst.hasInteriorVolume   = true;
			inst.receivesAudioReverb = true;
			inst.allowsWaterIngress  = false;

			PlaceCaveCommand::Data data;
			data.instance = std::move(inst);
			// Pas de splatPatch côté zone preset (camouflage = follow-up).

			outCmd = std::make_unique<PlaceCaveCommand>(
				ctx.meshInserts, ctx.terrain, std::move(data));
			return DispatchResult::Ok;
		}

		// --- place_overhang -------------------------------------------------

		DispatchResult DispatchPlaceOverhang(const OperationParams& params,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			std::string catalogId;
			if (!params.GetString("catalogId", catalogId) || catalogId.empty())
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_overhang : catalogId manquant");
				return DispatchResult::Failed;
			}
			const auto* entry = ctx.overhangCatalog.FindById(catalogId);
			if (entry == nullptr)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_overhang : catalogId '{}' introuvable",
					catalogId);
				return DispatchResult::Failed;
			}
			Vec3 worldPos{};
			if (!ReadWorldPos(params, worldPos))
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_overhang : worldPosition manquant");
				return DispatchResult::Failed;
			}
			const float yawDeg  = ReadFloat(params, "wallNormalYawDeg",
				ReadFloat(params, "rotationY", 0.0f));
			const float tiltDeg = ReadFloat(params, "tiltDeg", 0.0f);
			const float scale   = ReadFloat(params, "uniformScale", 1.0f);

			using engine::editor::world::volumes::MeshInsertInstance;
			using engine::editor::world::volumes::overhangs::PlaceOverhangCommand;

			MeshInsertInstance inst;
			inst.guid                = 0u;
			inst.gltfRelativePath    = entry->gltfRelativePath;
			inst.worldPosition       = worldPos;
			inst.eulerRotationDeg    = { 0.0f, yawDeg, tiltDeg };
			inst.uniformScale        = scale;
			inst.insertCategory      = "overhang";
			inst.displayName         = entry->displayName.empty() ? entry->id : entry->displayName;
			inst.hasInteriorVolume   = false;
			inst.allowsWaterIngress  = false;

			outCmd = std::make_unique<PlaceOverhangCommand>(ctx.meshInserts, std::move(inst));
			return DispatchResult::Ok;
		}

		// --- place_arch -----------------------------------------------------

		/// Lit un Vec3 (3 floats aplatis) depuis `params` sous la clé `key`.
		bool ReadVec3Key(const OperationParams& params, const char* key, Vec3& out)
		{
			std::vector<double> v;
			if (!params.GetNumberList(key, v) || v.size() < 3u) return false;
			out.x = static_cast<float>(v[0]);
			out.y = static_cast<float>(v[1]);
			out.z = static_cast<float>(v[2]);
			return true;
		}

		DispatchResult DispatchPlaceArch(const OperationParams& params,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			std::string catalogId;
			if (!params.GetString("catalogId", catalogId) || catalogId.empty())
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_arch : catalogId manquant");
				return DispatchResult::Failed;
			}
			const auto* entry = ctx.archCatalog.FindById(catalogId);
			if (entry == nullptr)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_arch : catalogId '{}' introuvable",
					catalogId);
				return DispatchResult::Failed;
			}
			Vec3 pillarA{}, pillarB{};
			if (!ReadVec3Key(params, "pillarA", pillarA)
				|| !ReadVec3Key(params, "pillarB", pillarB))
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_arch : pillarA/B manquants");
				return DispatchResult::Failed;
			}

			// Géométrie dérivée (mirror de ArchTool::Place) :
			//   worldPosition = midpoint(A, B),
			//   eulerRotationDeg.y = atan2(Bz-Az, Bx-Ax) en degrés,
			//   uniformScale = span_world / span_natif (clampé > 0).
			constexpr float kRadToDeg = 57.29577951308232f;
			const float dx = pillarB.x - pillarA.x;
			const float dz = pillarB.z - pillarA.z;
			const float spanWorld  = std::sqrt(dx * dx + dz * dz);
			const float spanNative = entry->NativeSpanMeters();
			const float scale = (spanNative > 0.001f && spanWorld > 0.001f)
				? (spanWorld / spanNative) : 1.0f;

			using engine::editor::world::volumes::MeshInsertInstance;
			using engine::editor::world::volumes::arches::PlaceArchCommand;

			MeshInsertInstance inst;
			inst.guid              = 0u;
			inst.gltfRelativePath  = entry->gltfRelativePath;
			inst.worldPosition.x   = 0.5f * (pillarA.x + pillarB.x);
			inst.worldPosition.y   = 0.5f * (pillarA.y + pillarB.y);
			inst.worldPosition.z   = 0.5f * (pillarA.z + pillarB.z);
			inst.eulerRotationDeg  = { 0.0f, std::atan2(dz, dx) * kRadToDeg, 0.0f };
			inst.uniformScale      = scale;
			inst.insertCategory    = "arch";
			inst.displayName       = entry->displayName.empty() ? entry->id : entry->displayName;
			inst.hasInteriorVolume   = false;
			inst.receivesAudioReverb = false;
			inst.allowsWaterIngress  = false;

			outCmd = std::make_unique<PlaceArchCommand>(ctx.meshInserts, std::move(inst));
			return DispatchResult::Ok;
		}

		// --- macros polyline (mountain_macro / valley_macro) ---------------

		/// Construit `MacroPolylineParams` à partir d'un polyline aplati
		/// `[x0, z0, x1, z1, …]` et de scalaires globaux width/height/noise.
		/// Les vertices héritent tous des mêmes valeurs (les presets n'ont
		/// pas de granularité per-vertex dans le format JSON M100.46 §A.2).
		/// Renvoie false si le polyline est invalide (< 2 vertices).
		bool BuildMacroParamsFromJson(const OperationParams& params,
			MacroPolylineParams& outParams)
		{
			std::vector<double> flat;
			if (!params.GetNumberList("polyline", flat) || flat.size() < 4
				|| (flat.size() % 2) != 0)
				return false;

			const float widthMeters    = ReadFloat(params, "widthMeters",    250.0f);
			const float heightMeters   = ReadFloat(params, "heightMeters",   400.0f);
			const float noiseAmplitude = ReadFloat(params, "noiseAmplitude",  30.0f);
			const float asymmetry      = ReadFloat(params, "asymmetry",        0.0f);

			outParams.vertices.clear();
			outParams.vertices.reserve(flat.size() / 2);
			for (size_t i = 0; i + 1 < flat.size(); i += 2)
			{
				PolylineVertex v;
				v.worldX         = static_cast<float>(flat[i]);
				v.worldZ         = static_cast<float>(flat[i + 1]);
				v.widthMeters    = widthMeters;
				v.heightMeters   = heightMeters;
				v.noiseAmplitude = noiseAmplitude;
				v.asymmetry      = asymmetry;
				outParams.vertices.push_back(v);
			}
			outParams.mode           = PolylineMode::Open;
			outParams.profile        = FlankProfile::Smoothstep;
			outParams.noiseSeed      = static_cast<uint32_t>(
				ReadFloat(params, "noiseSeed", 0.0f));
			outParams.noiseFrequency = ReadFloat(params, "noiseFrequency", 0.005f);
			return true;
		}

		DispatchResult DispatchMountainMacro(const OperationParams& params,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			MacroPolylineParams macroParams;
			if (!BuildMacroParamsFromJson(params, macroParams))
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] mountain_macro : polyline invalide");
				return DispatchResult::Failed;
			}
			auto deltas = RasterizeMacroPolyline(macroParams, /*invert*/ false);
			if (deltas.empty())
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] mountain_macro : rasterisation vide");
				return DispatchResult::Failed;
			}
			outCmd = std::make_unique<MountainRangeCommand>(ctx.terrain, std::move(deltas));
			return DispatchResult::Ok;
		}

		DispatchResult DispatchValleyMacro(const OperationParams& params,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			MacroPolylineParams macroParams;
			if (!BuildMacroParamsFromJson(params, macroParams))
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] valley_macro : polyline invalide");
				return DispatchResult::Failed;
			}
			auto deltas = RasterizeMacroPolyline(macroParams, /*invert*/ true);
			if (deltas.empty())
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] valley_macro : rasterisation vide");
				return DispatchResult::Failed;
			}
			outCmd = std::make_unique<ValleyChainCommand>(ctx.terrain, std::move(deltas));
			return DispatchResult::Ok;
		}

		// --- lake_polygon --------------------------------------------------

		DispatchResult DispatchLakePolygon(const OperationParams& params,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			std::vector<double> flat;
			if (!params.GetNumberList("polygon", flat) || flat.size() < 6
				|| (flat.size() % 2) != 0)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] lake_polygon : polygon invalide (besoin ≥ 3 points)");
				return DispatchResult::Failed;
			}
			const float waterLevel = ReadFloat(params, "waterLevel", 0.0f);

			engine::world::water::LakeInstance lake;
			std::string name;
			if (params.GetString("name", name)) lake.name = std::move(name);
			lake.waterLevelY = waterLevel;
			lake.polygon.reserve(flat.size() / 2);
			for (size_t i = 0; i + 1 < flat.size(); i += 2)
			{
				lake.polygon.push_back(engine::math::Vec3{
					static_cast<float>(flat[i]),
					waterLevel,
					static_cast<float>(flat[i + 1])
				});
			}
			// turbidity / bottomColor / isOcean → défauts struct.

			outCmd = std::make_unique<AddLakeCommand>(ctx.water, std::move(lake));
			return DispatchResult::Ok;
		}

		// --- river_manual --------------------------------------------------

		DispatchResult DispatchRiverManual(const OperationParams& params,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			std::vector<double> flat;
			if (!params.GetNumberList("polyline", flat) || flat.size() < 4
				|| (flat.size() % 2) != 0)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] river_manual : polyline invalide (besoin ≥ 2 points)");
				return DispatchResult::Failed;
			}
			const float widthMeters = ReadFloat(params, "widthMeters", 4.0f);
			const float depthMeters = ReadFloat(params, "depthMeters", 1.0f);
			// Y = 0 par convention MVP : le runtime client recalcule l'élévation
			// depuis la heightmap au load (TODO : lookup TerrainDocument quand
			// l'éditeur expose une API headless TerrainDocument::SampleHeight).

			engine::world::water::RiverInstance river;
			std::string name;
			if (params.GetString("name", name)) river.name = std::move(name);
			river.nodes.reserve(flat.size() / 2);
			for (size_t i = 0; i + 1 < flat.size(); i += 2)
			{
				engine::world::water::RiverNode node;
				node.position = engine::math::Vec3{
					static_cast<float>(flat[i]),
					0.0f,
					static_cast<float>(flat[i + 1])
				};
				node.widthMeters = widthMeters;
				node.depthMeters = depthMeters;
				river.nodes.push_back(node);
			}

			outCmd = std::make_unique<AddRiverCommand>(ctx.water, std::move(river));
			return DispatchResult::Ok;
		}

		// --- Sim helpers ---------------------------------------------------

		// L'assemblage du `ConsolidatedHeightGrid` 2×2 chunks vit maintenant
		// dans `water/HeightGridAssembly.{h,cpp}` (partagé avec
		// CoastlineEditorTool et ThermalWindErosionTool). On le réutilise tel
		// quel via `engine::editor::world::BuildGridFromLoadedChunks(...)`.

		/// Fusionne `src` dans `dst` cellule par cellule (les valeurs s'additionnent).
		void MergeDeltas(engine::editor::world::SparseChunkDeltas& dst,
			const engine::editor::world::SparseChunkDeltas& src)
		{
			for (const auto& kv : src)
			{
				auto& chunkMap = dst[kv.first];
				for (const auto& cell : kv.second)
					chunkMap[cell.first] += cell.second;
			}
		}

		/// Si la clé `rngSeed` est une chaîne "global", utilise `custom.seed`.
		/// Sinon, lit la valeur numérique. Défaut : `fallback`.
		uint32_t ReadRngSeed(const OperationParams& params,
			const CustomizationParams& custom, uint32_t fallback)
		{
			std::string s;
			if (params.GetString("rngSeed", s) && s == "global")
				return custom.seed;
			double v = 0.0;
			if (params.GetNumber("rngSeed", v))
				return static_cast<uint32_t>(v < 0.0 ? 0.0 : v);
			return fallback;
		}

		/// Lit la clé `"preset"` du JSON op et applique l'overlay de preset
		/// si trouvée dans le `ToolPresetRegistry`. M100.46 incrément 2e+ :
		/// connecte les `tool_presets/<toolId>.json` (chargés au boot) aux
		/// dispatchers, pour que `"preset":"subtle"` (hydraulic) ou
		/// `"preset":"sand_and_talus"` (thermal_wind) charge réellement les
		/// physics params, pas seulement les scalaires explicites du JSON.
		///
		/// Ordre du remplissage des params : defaults struct → preset
		/// overlay (ici) → JSON scalar overrides (après l'appel).
		/// JSON gagne toujours sur preset.
		///
		/// \param toolId       id du tool dans le registry (ex. "hydraulic_erosion")
		/// \param jsonParams   params de l'op (lit la clé "preset")
		/// \param applyFn      fonctor `void(const ToolPreset&)` qui appelle
		///                     l'ApplyXxxPreset typé sur le struct cible.
		/// Effet de bord : log warn si `presetId` non vide mais introuvable.
		template <typename ApplyFn>
		void MaybeApplyToolPreset(const std::string& toolId,
			const OperationParams& jsonParams, ApplyFn&& applyFn)
		{
			std::string presetId;
			if (!jsonParams.GetString("preset", presetId) || presetId.empty())
				return;
			const auto* tp = engine::editor::world::presets::ToolPresetRegistry::Instance()
				.FindPreset(toolId, presetId);
			if (tp == nullptr)
			{
				LOG_WARN(EditorWorld,
					"[OperationDispatcher] preset '{}' introuvable pour tool '{}'",
					presetId, toolId);
				return;
			}
			applyFn(*tp);
		}

		// --- hydraulic_erosion --------------------------------------------

		DispatchResult DispatchHydraulicErosion(const OperationParams& params,
			const CustomizationParams& custom,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			if (ctx.config == nullptr)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] hydraulic_erosion : Config absent du DispatchContext");
				return DispatchResult::Failed;
			}

			using namespace engine::editor::world::erosion;
			HydraulicSimulationParams sim;

			// Preset overlay (defaults → preset → JSON scalars).
			MaybeApplyToolPreset("hydraulic_erosion", params,
				[&](const engine::editor::world::presets::ToolPreset& tp) {
					engine::editor::world::presets::ApplyHydraulicErosionPreset(sim, tp);
				});

			sim.numDroplets = static_cast<uint32_t>(
				ReadFloat(params, "numDroplets", static_cast<float>(sim.numDroplets)));
			sim.maxLifetimeSteps = static_cast<uint32_t>(
				ReadFloat(params, "maxLifetimeSteps", static_cast<float>(sim.maxLifetimeSteps)));
			sim.sedimentCapacity = ReadFloat(params, "sedimentCapacity", sim.sedimentCapacity);
			sim.erosionRate      = ReadFloat(params, "erosionRate",      sim.erosionRate);
			sim.depositionRate   = ReadFloat(params, "depositionRate",   sim.depositionRate);
			sim.evaporationRate  = ReadFloat(params, "evaporationRate",  sim.evaporationRate);
			sim.gravity          = ReadFloat(params, "gravity",          sim.gravity);
			sim.inertia          = ReadFloat(params, "inertia",          sim.inertia);
			sim.rngSeed          = ReadRngSeed(params, custom, sim.rngSeed);

			auto grid = BuildGridFromLoadedChunks(ctx.terrain, *ctx.config);
			const float seaLevel = ctx.water.GetOcean().seaLevelMeters;

			auto result = RunHydraulicOnGrid(grid, seaLevel, sim);
			outCmd = std::make_unique<HydraulicErosionCommand>(ctx.terrain,
				std::move(result), sim);
			return DispatchResult::Ok;
		}

		// --- thermal_wind_erosion ------------------------------------------

		DispatchResult DispatchThermalWindErosion(const OperationParams& params,
			const CustomizationParams& custom,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			if (ctx.config == nullptr)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] thermal_wind_erosion : Config absent");
				return DispatchResult::Failed;
			}

			using namespace engine::editor::world::erosion;
			const bool thermalEnabled = ReadBool(params, "thermalEnabled", true);
			const bool windEnabled    = ReadBool(params, "windEnabled",    true);
			if (!thermalEnabled && !windEnabled)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] thermal_wind_erosion : aucune passe activée");
				return DispatchResult::Failed;
			}

			// Wrapper attendu par ApplyThermalWindErosionPreset. Les
			// scalaires JSON viendront overrider après l'overlay preset.
			ThermalWindErosionParams combined;
			MaybeApplyToolPreset("thermal_wind_erosion", params,
				[&](const engine::editor::world::presets::ToolPreset& tp) {
					engine::editor::world::presets::ApplyThermalWindErosionPreset(combined, tp);
				});
			ThermalSimulationParams& thermalP = combined.thermal;
			WindSimulationParams&    windP    = combined.wind;

			thermalP.talusAngleDeg  = ReadFloat(params, "talusAngleDeg",  thermalP.talusAngleDeg);
			thermalP.forcePerPass   = ReadFloat(params, "forcePerPass",   thermalP.forcePerPass);
			thermalP.numPasses      = static_cast<uint32_t>(
				ReadFloat(params, "numPasses", static_cast<float>(thermalP.numPasses)));

			windP.windAngleDeg   = ReadFloat(params, "windAngleDeg",   windP.windAngleDeg);
			windP.windStrength   = ReadFloat(params, "windStrength",   windP.windStrength);
			windP.numParticles   = static_cast<uint32_t>(
				ReadFloat(params, "numParticles", static_cast<float>(windP.numParticles)));
			windP.rngSeed        = ReadRngSeed(params, custom, windP.rngSeed);

			auto grid = BuildGridFromLoadedChunks(ctx.terrain, *ctx.config);
			const float seaLevel = ctx.water.GetOcean().seaLevelMeters;

			ThermalWindErosionCommand::Data data;
			if (thermalEnabled)
			{
				auto r = RunThermalSimulation(grid, seaLevel, thermalP);
				data.thermalDeltas = r.deltas;
				data.thermalStats  = r;
			}
			if (windEnabled)
			{
				// `grid` muté par thermal sert d'input à wind (ordre respecté).
				auto r = RunWindSimulation(grid, seaLevel, windP);
				data.windDeltas = r.deltas;
				data.windStats  = r;
			}

			outCmd = std::make_unique<ThermalWindErosionCommand>(ctx.terrain, std::move(data));
			return DispatchResult::Ok;
		}

		// --- river_network -------------------------------------------------

		DispatchResult DispatchRiverNetwork(const OperationParams& params,
			const CustomizationParams& custom,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			(void)custom; // pas de customisation directe ici (water_density déjà appliqué upstream)
			if (ctx.config == nullptr)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] river_network : Config absent");
				return DispatchResult::Failed;
			}

			std::vector<double> sources;
			if (!params.GetNumberList("sources", sources) || sources.size() < 2
				|| (sources.size() % 2) != 0)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] river_network : sources invalides (besoin ≥ 1 paire [x,z])");
				return DispatchResult::Failed;
			}

			engine::editor::world::WatershedSimulationParams sim;

			// Preset overlay (laisse `springs` intacts, conforme au
			// docstring d'ApplyRiverNetworkPreset).
			MaybeApplyToolPreset("river_network", params,
				[&](const engine::editor::world::presets::ToolPreset& tp) {
					engine::editor::world::presets::ApplyRiverNetworkPreset(sim, tp);
				});

			sim.springs.reserve(sources.size() / 2);
			for (size_t i = 0; i + 1 < sources.size(); i += 2)
			{
				engine::editor::world::SpringSource s;
				s.worldX = static_cast<float>(sources[i]);
				s.worldZ = static_cast<float>(sources[i + 1]);
				s.worldY = 0.0f; // résolu par re-sampling dans le simulateur
				sim.springs.push_back(s);
			}
			sim.carveHeightmap = ReadBool(params, "carvingEnabled", sim.carveHeightmap);
			sim.minFlowThresholdCells = static_cast<uint32_t>(
				ReadFloat(params, "minFlowThresholdCells",
					static_cast<float>(sim.minFlowThresholdCells)));

			auto grid = BuildGridFromLoadedChunks(ctx.terrain, *ctx.config);
			const float seaLevel = ctx.water.GetOcean().seaLevelMeters;

			auto result = engine::editor::world::RunWatershedOnGrid(grid, seaLevel, sim);
			const auto& currentOcean = ctx.water.GetOcean();
			outCmd = std::make_unique<engine::editor::world::RiverNetworkCommand>(
				ctx.terrain, ctx.water, std::move(result),
				currentOcean, currentOcean);
			return DispatchResult::Ok;
		}

		// --- coastline -----------------------------------------------------

		DispatchResult DispatchCoastline(const OperationParams& params,
			const CustomizationParams& custom,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			(void)custom;
			if (ctx.config == nullptr)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] coastline : Config absent");
				return DispatchResult::Failed;
			}

			// Construit OceanSettings depuis le JSON, défauts depuis la valeur
			// courante (cohérent avec l'usage du tool — l'utilisateur ne
			// renseigne souvent que seaLevelMeters).
			engine::editor::world::OceanSettings newOcean = ctx.water.GetOcean();
			newOcean.seaLevelMeters = ReadFloat(params, "seaLevelMeters",
				newOcean.seaLevelMeters);
			newOcean.turbidity      = ReadFloat(params, "turbidity",      newOcean.turbidity);
			newOcean.windInfluence  = ReadFloat(params, "windInfluence",  newOcean.windInfluence);
			newOcean.enabled        = ReadBool(params, "oceanEnabled",    newOcean.enabled);

			const bool smoothingEnabled = ReadBool(params, "beachEnabled", true);
			const bool cliffsEnabled    = ReadBool(params, "cliffsEnabled", false);

			engine::editor::world::CoastlineCommand::ApplyData data;
			data.previousOcean = ctx.water.GetOcean();
			data.newOcean      = newOcean;

			// Recherche / insertion de la LakeInstance océan (mêmes
			// défauts visuels que CoastlineEditorTool : marge 1000 m,
			// rectangle englobant + 1 pt de fermeture).
			const auto& scene = ctx.water.Get();
			int existingIndex = -1;
			for (size_t i = 0; i < scene.lakes.size(); ++i)
			{
				if (scene.lakes[i].isOcean)
				{
					existingIndex = static_cast<int>(i);
					break;
				}
			}
			data.existingOceanIndex = existingIndex;
			if (existingIndex >= 0)
			{
				data.previousOceanLake = scene.lakes[static_cast<size_t>(existingIndex)];
			}
			else
			{
				constexpr float kOceanMarginMeters = 1000.0f;
				constexpr float kZoneSizeMeters =
					static_cast<float>(engine::world::kZoneSize);
				engine::world::water::LakeInstance lake;
				lake.name = "ocean";
				lake.polygon = {
					{ -kOceanMarginMeters, newOcean.seaLevelMeters, -kOceanMarginMeters },
					{ kZoneSizeMeters + kOceanMarginMeters, newOcean.seaLevelMeters, -kOceanMarginMeters },
					{ kZoneSizeMeters + kOceanMarginMeters, newOcean.seaLevelMeters,  kZoneSizeMeters + kOceanMarginMeters },
					{ -kOceanMarginMeters, newOcean.seaLevelMeters,  kZoneSizeMeters + kOceanMarginMeters },
					{ -kOceanMarginMeters, newOcean.seaLevelMeters, -kOceanMarginMeters },
				};
				lake.waterLevelY = newOcean.seaLevelMeters;
				lake.bottomColor = engine::math::Vec3{
					newOcean.bottomColor[0],
					newOcean.bottomColor[1],
					newOcean.bottomColor[2]
				};
				lake.turbidity = newOcean.turbidity;
				lake.isOcean   = true;
				data.oceanToInsert = std::move(lake);
			}

			if (smoothingEnabled || cliffsEnabled)
			{
				auto grid = BuildGridFromLoadedChunks(ctx.terrain, *ctx.config);
				if (smoothingEnabled)
				{
					const float bandM  = ReadFloat(params, "smoothingBandMeters", 5.0f);
					const float forceF = ReadFloat(params, "smoothingForce",      0.3f);
					auto deltas = engine::editor::world::ComputeCoastlineSmoothingDeltas(
						grid, newOcean.seaLevelMeters, bandM, forceF);
					MergeDeltas(data.heightmapDeltas, deltas);
				}
				if (cliffsEnabled)
				{
					const float thresholdM  = ReadFloat(params, "cliffsThresholdMeters",   8.0f);
					const float slopeDeg    = ReadFloat(params, "cliffsSlopeThresholdDeg", 45.0f);
					const float landSideM   = ReadFloat(params, "cliffsLandSideMeters",    6.0f);
					const float seaSideM    = ReadFloat(params, "cliffsSeaSideMeters",     3.0f);
					auto deltas = engine::editor::world::ComputeCoastlineCliffsDeltas(
						grid, newOcean.seaLevelMeters,
						thresholdM, slopeDeg, landSideM, seaSideM);
					MergeDeltas(data.heightmapDeltas, deltas);
				}
			}

			outCmd = std::make_unique<engine::editor::world::CoastlineCommand>(
				ctx.terrain, ctx.water, std::move(data));
			return DispatchResult::Ok;
		}

		// --- place_dungeon --------------------------------------------------

		DispatchResult DispatchPlaceDungeon(const OperationParams& params,
			const DispatchContext& ctx,
			std::unique_ptr<engine::editor::world::ICommand>& outCmd)
		{
			std::string catalogId;
			if (!params.GetString("catalogId", catalogId) || catalogId.empty())
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_dungeon : catalogId manquant");
				return DispatchResult::Failed;
			}
			const auto* entry = ctx.dungeonCatalog.FindById(catalogId);
			if (entry == nullptr)
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_dungeon : catalogId '{}' introuvable",
					catalogId);
				return DispatchResult::Failed;
			}
			Vec3 worldPos{};
			if (!ReadWorldPos(params, worldPos))
			{
				LOG_WARN(EditorWorld, "[OperationDispatcher] place_dungeon : worldPosition manquant");
				return DispatchResult::Failed;
			}
			std::string dungeonTemplateId;
			if (!params.GetString("dungeonId", dungeonTemplateId) || dungeonTemplateId.empty())
			{
				dungeonTemplateId = entry->id; // défaut : id du catalog
			}

			using engine::editor::world::volumes::dungeons::DungeonPortalInstance;
			using engine::editor::world::volumes::dungeons::PlaceDungeonPortalCommand;

			DungeonPortalInstance inst;
			inst.guid              = 0u;
			inst.dungeonTemplateId = dungeonTemplateId;
			inst.displayName       = entry->displayName.empty() ? entry->id : entry->displayName;
			inst.decorativeMeshPath = entry->decorativeMeshPath;
			inst.worldPosition     = worldPos;
			inst.eulerRotationDeg  = { 0.0f, ReadFloat(params, "rotationY", 0.0f), 0.0f };
			inst.triggerRadius     = ReadFloat(params, "triggerRadius", 3.0f);
			inst.requiredLevel     = static_cast<uint16_t>(
				ReadFloat(params, "requiredLevel", static_cast<float>(entry->requiredLevel)));
			inst.minDifficulty     = entry->minDifficulty;
			inst.maxDifficulty     = entry->maxDifficulty;

			outCmd = std::make_unique<PlaceDungeonPortalCommand>(ctx.dungeonPortals, std::move(inst));
			return DispatchResult::Ok;
		}
	}

	DispatchResult DispatchOperation(const ZonePresetOperation& op,
		const CustomizationParams& custom,
		const DispatchContext& ctx,
		std::unique_ptr<engine::editor::world::ICommand>& outCmd)
	{
		outCmd.reset();

		// Parse les params + applique la customisation (multiplicateurs
		// sur les champs scalaires selon op.affectedBy).
		OperationParams params = OperationParams::Parse(op.rawJson);
		ApplyCustomization(params, op.affectedBy, custom);

		if (op.type == "place_cave")     return DispatchPlaceCave(params, ctx, outCmd);
		if (op.type == "place_overhang") return DispatchPlaceOverhang(params, ctx, outCmd);
		if (op.type == "place_arch")     return DispatchPlaceArch(params, ctx, outCmd);
		if (op.type == "place_dungeon")  return DispatchPlaceDungeon(params, ctx, outCmd);

		// Incrément 2d — types câblés via wrappers ou commandes plain-data.
		if (op.type == "mountain_macro") return DispatchMountainMacro(params, ctx, outCmd);
		if (op.type == "valley_macro")   return DispatchValleyMacro(params, ctx, outCmd);
		if (op.type == "lake_polygon")   return DispatchLakePolygon(params, ctx, outCmd);
		if (op.type == "river_manual")   return DispatchRiverManual(params, ctx, outCmd);

		// Incrément 2e — câblage des 4 ops simulation via les variantes
		// pures `Run*OnGrid` / `RunThermalSimulation` / `RunWindSimulation`
		// + le helper local `BuildGridFromLoadedChunks`. 14/14 types câblés.
		if (op.type == "hydraulic_erosion")     return DispatchHydraulicErosion(params, custom, ctx, outCmd);
		if (op.type == "thermal_wind_erosion")  return DispatchThermalWindErosion(params, custom, ctx, outCmd);
		if (op.type == "river_network")         return DispatchRiverNetwork(params, custom, ctx, outCmd);
		if (op.type == "coastline")             return DispatchCoastline(params, custom, ctx, outCmd);

		// `sculpt_brush` et `splat_paint` connus mais pas câblés (les 8
		// presets livrés ne les utilisent pas et leurs ICommand sont des
		// actions ponctuelles, pas du batch déterministe). Skip silencieux
		// aligné sur le comportement §A.4 pour `decoration`.
		LOG_INFO(EditorWorld,
			"[OperationDispatcher] type '{}' connu mais pas câblé en M100.46 (sculpt_brush/splat_paint = ponctuel, pas batch)",
			op.type);
		return DispatchResult::Unsupported;
	}
}
