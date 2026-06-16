#pragma once

// Auberge éditable (T2) — Instanciation : applique un transform de groupe
// (pivot monde + yaw) à un preset et produit des PropInstance posables, plus
// l'ancre de spawn en coordonnées monde.

#include <cstdint>
#include <functional>
#include <vector>

#include "src/client/world/instances/PropInstances.h"
#include "src/world_editor/structures/BuildingPreset.h"

namespace engine::editor::world::structures
{
	/// Fait tourner un offset (m) autour de l'axe Y de `yawDeg` degrés.
	/// Convention : yaw horaire vu de dessus (x' = x cosθ + z sinθ ;
	/// z' = -x sinθ + z cosθ). Cohérente avec l'export world.scenery (yaw_deg).
	engine::math::Vec3 RotateYaw(const engine::math::Vec3& v, float yawDeg);

	/// Instancie `preset` au pivot monde `pivot` (m) avec rotation de groupe
	/// `groupYawDeg`. Chaque élément devient une PropInstance : position =
	/// pivot + RotateYaw(offset), yaw monde = groupYaw + élément.yaw, assetId =
	/// HashAssetPath(meshPath), layerTag = Structures, groupId = `groupId`,
	/// instanceId alloué via `allocInstanceId`.
	/// \param allocInstanceId foncteur retournant un instanceId unique (zone).
	std::vector<engine::world::instances::PropInstance> InstantiatePreset(
		const BuildingPreset& preset, const engine::math::Vec3& pivot,
		float groupYawDeg, uint32_t groupId,
		const std::function<uint32_t()>& allocInstanceId);

	/// Position monde (m) de l'ancre de spawn : pivot + RotateYaw(spawnAnchor).
	engine::math::Vec3 SpawnAnchorWorld(const BuildingPreset& preset,
		const engine::math::Vec3& pivot, float groupYawDeg);
}
