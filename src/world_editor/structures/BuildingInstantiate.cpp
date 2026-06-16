#include "src/world_editor/structures/BuildingInstantiate.h"

#include <cmath>

#include "src/world_editor/PlacementGeometry.h"
#include "src/world_editor/PlacementTool.h" // HashAssetPath

namespace engine::editor::world::structures
{
	namespace pg = engine::editor::world::placement;

	engine::math::Vec3 RotateYaw(const engine::math::Vec3& v, float yawDeg)
	{
		const float r = yawDeg * 3.14159265358979323846f / 180.0f;
		const float c = std::cos(r), s = std::sin(r);
		return engine::math::Vec3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
	}

	std::vector<engine::world::instances::PropInstance> InstantiatePreset(
		const BuildingPreset& preset, const engine::math::Vec3& pivot,
		float groupYawDeg, uint32_t groupId,
		const std::function<uint32_t()>& allocInstanceId)
	{
		using engine::world::instances::PropInstance;
		using engine::world::instances::PlacementLayer;
		std::vector<PropInstance> out;
		out.reserve(preset.elements.size());
		for (const BuildingPresetElement& e : preset.elements)
		{
			PropInstance inst;
			const engine::math::Vec3 rot = RotateYaw(e.offset, groupYawDeg);
			inst.position = engine::math::Vec3(
				pivot.x + rot.x, pivot.y + rot.y, pivot.z + rot.z);
			const float worldYaw = groupYawDeg + e.yawDeg;
			pg::BuildOrientation(worldYaw, engine::math::Vec3(0, 1, 0), false,
				inst.rotationQuat);
			inst.assetId = engine::editor::world::HashAssetPath(e.meshPath);
			inst.scale = engine::math::Vec3(e.scale, e.scale, e.scale);
			inst.layerTag = static_cast<uint32_t>(PlacementLayer::Structures);
			inst.groupId = groupId;
			inst.instanceId = allocInstanceId ? allocInstanceId() : 0u;
			out.push_back(inst);
		}
		return out;
	}

	engine::math::Vec3 SpawnAnchorWorld(const BuildingPreset& preset,
		const engine::math::Vec3& pivot, float groupYawDeg)
	{
		const engine::math::Vec3 r = RotateYaw(preset.spawnAnchor, groupYawDeg);
		return engine::math::Vec3(pivot.x + r.x, pivot.y + r.y, pivot.z + r.z);
	}
}
