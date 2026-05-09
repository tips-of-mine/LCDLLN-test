// src/client/world/collision/AutoFitProxy.cpp
#include "src/client/world/collision/AutoFitProxy.h"

#include <algorithm>
#include <cfloat>

namespace engine::world::collision
{
	CollisionProxy AutoFit(const CollisionMeshCpu& mesh)
	{
		CollisionProxy out;

		if (mesh.vertices.empty())
		{
			// Fallback : capsule par défaut. Le caller a donné un mesh vide.
			return out;
		}

		// 1. Bounding box AABB
		engine::math::Vec3 bmin{ +FLT_MAX, +FLT_MAX, +FLT_MAX };
		engine::math::Vec3 bmax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
		for (const auto& v : mesh.vertices)
		{
			bmin.x = std::min(bmin.x, v.x); bmax.x = std::max(bmax.x, v.x);
			bmin.y = std::min(bmin.y, v.y); bmax.y = std::max(bmax.y, v.y);
			bmin.z = std::min(bmin.z, v.z); bmax.z = std::max(bmax.z, v.z);
		}

		const float height   = bmax.y - bmin.y;
		const float widthX   = bmax.x - bmin.x;
		const float widthZ   = bmax.z - bmin.z;
		const float widthMax = std::max(widthX, widthZ);

		// 2. Dispatch
		if (mesh.isStatic || mesh.vertices.size() > 500u)
		{
			out.type = ProxyType::TriMesh;
			out.vertices = mesh.vertices;
			out.indices  = mesh.indices;
		}
		else if (widthMax > 0.0f && (height / widthMax) > 3.0f)
		{
			out.type = ProxyType::Capsule;
			const float r = widthMax * 0.5f;
			const engine::math::Vec3 center{
				(bmin.x + bmax.x) * 0.5f, 0.0f, (bmin.z + bmax.z) * 0.5f };
			out.capsuleA      = engine::math::Vec3{ center.x, bmin.y + r, center.z };
			out.capsuleB      = engine::math::Vec3{ center.x, bmax.y - r, center.z };
			out.capsuleRadius = r;
		}
		else
		{
			out.type = ProxyType::ConvexHull;
			// 8 vertices du bounding box (placeholder pour vrai quickhull)
			out.vertices = {
				{ bmin.x, bmin.y, bmin.z }, { bmax.x, bmin.y, bmin.z },
				{ bmin.x, bmax.y, bmin.z }, { bmax.x, bmax.y, bmin.z },
				{ bmin.x, bmin.y, bmax.z }, { bmax.x, bmin.y, bmax.z },
				{ bmin.x, bmax.y, bmax.z }, { bmax.x, bmax.y, bmax.z },
			};
		}

		return out;
	}
}
