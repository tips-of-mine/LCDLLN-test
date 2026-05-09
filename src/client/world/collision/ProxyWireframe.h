// engine/world/collision/ProxyWireframe.h
#pragma once

#include "engine/math/Math.h"
#include "engine/world/collision/CollisionProxy.h"

#include <utility>
#include <vector>

namespace engine::world::collision
{
	using Edge3D = std::pair<engine::math::Vec3, engine::math::Vec3>;

	/// Génère les arêtes 3D du wireframe d'un proxy (M100.12). Pour :
	///  - Capsule : 2 cap rings (16 segments chacun) + 4 longitudinal lines = 36 edges
	///  - ConvexHull : 12 edges du bounding box (assume 8 vertices structuraux)
	///  - TriMesh : 3 edges par triangle (peut être beaucoup, sans dédup MVP)
	///
	/// Pure function, aucune allocation Vulkan, aucune dépendance externe.
	/// Consommé par `CollisionEditorPanel` pour le mini-preview ImGui DrawList.
	std::vector<Edge3D> GenerateWireframeEdges(const CollisionProxy& proxy);
}
