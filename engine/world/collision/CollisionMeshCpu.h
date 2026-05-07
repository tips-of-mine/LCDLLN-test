// engine/world/collision/CollisionMeshCpu.h
#pragma once

#include "engine/math/Math.h"

#include <cstdint>
#include <vector>

namespace engine::world::collision
{
	/// Représentation CPU minimale d'un mesh consommée par AutoFit (M100.12).
	/// Pas de matériaux, UVs, normales — juste géométrie.
	/// Le caller fournit les données (pas de loader .obj/.gltf en M100.12).
	struct CollisionMeshCpu
	{
		std::vector<engine::math::Vec3> vertices;
		std::vector<uint32_t>           indices;
		bool                            isStatic = false; // hint pour le dispatch
	};
}
