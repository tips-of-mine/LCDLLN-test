// engine/world/collision/AutoFitProxy.h
#pragma once

#include "engine/world/collision/CollisionMeshCpu.h"
#include "engine/world/collision/CollisionProxy.h"

namespace engine::world::collision
{
	/// Choisit automatiquement un proxy à partir d'un mesh CPU (M100.12) :
	///  - Capsule si height/widthMax > 3 (mesh très vertical, ex. tronc d'arbre)
	///  - TriMesh si vertices.size() > 500 OU mesh.isStatic == true
	///  - ConvexHull (= bounding box 8 vertices) sinon
	///
	/// Note : ConvexHull est un placeholder bounding box. Un vrai quickhull
	/// viendra dans un follow-up si nécessaire pour le gameplay. Le ticket
	/// M100.12 dit explicitement "single pass" — le dispatch lui-même est
	/// le pass unique.
	///
	/// \param mesh Mesh CPU. Si vide, retourne une capsule par défaut.
	CollisionProxy AutoFit(const CollisionMeshCpu& mesh);
}
