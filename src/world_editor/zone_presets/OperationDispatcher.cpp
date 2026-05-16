#include "src/world_editor/zone_presets/OperationDispatcher.h"

#include "src/shared/core/Log.h"
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

		// Types connus mais non câblés en MVP incrément 2b — l'executor
		// logge et continue (comportement aligné sur le skip silencieux
		// de la section `decoration` §A.4).
		LOG_INFO(EditorWorld,
			"[OperationDispatcher] type '{}' connu mais non câblé en M100.46 incrément 2b (op ignorée)",
			op.type);
		return DispatchResult::Unsupported;
	}
}
