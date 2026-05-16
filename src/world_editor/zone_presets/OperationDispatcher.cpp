#include "src/world_editor/zone_presets/OperationDispatcher.h"

#include "src/client/world/water/WaterSurfaces.h"
#include "src/shared/core/Log.h"
#include "src/world_editor/terrain/MountainRangeCommand.h"
#include "src/world_editor/terrain/PolylineMacroCore.h"
#include "src/world_editor/terrain/ValleyChainCommand.h"
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
#include "src/world_editor/zone_presets/OperationParams.h"

#include <cmath>
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

		// Types connus mais non câblés (besoin d'extraire la simulation des
		// Tools UI ou de capturer un snapshot d'état pré-action) :
		// `coastline`, `river_network`, `hydraulic_erosion`,
		// `thermal_wind_erosion`. Comportement aligné sur le skip silencieux
		// de la section `decoration` §A.4.
		LOG_INFO(EditorWorld,
			"[OperationDispatcher] type '{}' connu mais non câblé en M100.46 incrément 2d (op ignorée)",
			op.type);
		return DispatchResult::Unsupported;
	}
}
