// engine/world/collision/ProxyWireframe.cpp
#include "engine/world/collision/ProxyWireframe.h"

#include <cmath>

namespace engine::world::collision
{
	namespace
	{
		constexpr int kCapsuleRingSegments = 16;

		void AddCapsuleEdges(const CollisionProxy& p, std::vector<Edge3D>& out)
		{
			const auto& a = p.capsuleA;
			const auto& b = p.capsuleB;
			const float r = p.capsuleRadius;

			// Direction de la capsule (normalisée)
			engine::math::Vec3 axis = b - a;
			const float axisLen = axis.Length();
			if (axisLen <= 0.0f)
			{
				// Capsule dégénérée → axis Y par défaut
				axis = engine::math::Vec3{ 0.0f, 1.0f, 0.0f };
			}
			else
			{
				axis = axis.Normalized();
			}

			// Construire un repère orthonormé (axis, u, v)
			engine::math::Vec3 u;
			if (std::fabs(axis.y) < 0.9f)
				u = engine::math::Vec3{ axis.z, 0.0f, -axis.x }.Normalized();
			else
				u = engine::math::Vec3{ 1.0f, 0.0f, 0.0f };
			engine::math::Vec3 v{
				axis.y * u.z - axis.z * u.y,
				axis.z * u.x - axis.x * u.z,
				axis.x * u.y - axis.y * u.x,
			};

			// 2 cap rings : 1 centré sur a, 1 centré sur b. 16 segments chacun.
			for (int cap = 0; cap < 2; ++cap)
			{
				const auto& center = (cap == 0) ? a : b;
				engine::math::Vec3 prev{
					center.x + u.x * r,
					center.y + u.y * r,
					center.z + u.z * r,
				};
				for (int i = 1; i <= kCapsuleRingSegments; ++i)
				{
					const float t = static_cast<float>(i) / kCapsuleRingSegments * 6.2831853f;
					const float c = std::cos(t);
					const float s = std::sin(t);
					engine::math::Vec3 cur{
						center.x + (u.x * c + v.x * s) * r,
						center.y + (u.y * c + v.y * s) * r,
						center.z + (u.z * c + v.z * s) * r,
					};
					out.emplace_back(prev, cur);
					prev = cur;
				}
			}

			// 4 lignes longitudinales : 0°, 90°, 180°, 270°
			for (int i = 0; i < 4; ++i)
			{
				const float t = static_cast<float>(i) * 1.5707963f;
				const float c = std::cos(t);
				const float s = std::sin(t);
				engine::math::Vec3 offset{
					(u.x * c + v.x * s) * r,
					(u.y * c + v.y * s) * r,
					(u.z * c + v.z * s) * r,
				};
				engine::math::Vec3 pa{ a.x + offset.x, a.y + offset.y, a.z + offset.z };
				engine::math::Vec3 pb{ b.x + offset.x, b.y + offset.y, b.z + offset.z };
				out.emplace_back(pa, pb);
			}
		}

		/// Pour un ConvexHull avec 8 vertices structurés en bounding box (ordre :
		/// bmin/bmax XYZ comme dans AutoFit ConvexHull case), génère les 12
		/// arêtes du cube.
		void AddBoundingBoxEdges(const std::vector<engine::math::Vec3>& v,
			std::vector<Edge3D>& out)
		{
			// Convention vertex order (cf. AutoFit ConvexHull case) :
			//  0: (xMin, yMin, zMin)   1: (xMax, yMin, zMin)
			//  2: (xMin, yMax, zMin)   3: (xMax, yMax, zMin)
			//  4: (xMin, yMin, zMax)   5: (xMax, yMin, zMax)
			//  6: (xMin, yMax, zMax)   7: (xMax, yMax, zMax)
			static constexpr int edges[12][2] = {
				{0, 1}, {1, 3}, {3, 2}, {2, 0},  // face zMin
				{4, 5}, {5, 7}, {7, 6}, {6, 4},  // face zMax
				{0, 4}, {1, 5}, {2, 6}, {3, 7},  // verticales connectives
			};
			for (int i = 0; i < 12; ++i)
				out.emplace_back(v[edges[i][0]], v[edges[i][1]]);
		}

		void AddHullEdges(const CollisionProxy& p, std::vector<Edge3D>& out)
		{
			if (p.vertices.size() == 8)
			{
				AddBoundingBoxEdges(p.vertices, out);
			}
			else
			{
				// Fallback générique pour hulls non-bbox : connecte chaque
				// vertex au suivant. M100.12 ne génère pas ce cas, mais le
				// support défensif évite des arêtes manquantes en preview.
				for (size_t i = 0; i + 1 < p.vertices.size(); ++i)
					out.emplace_back(p.vertices[i], p.vertices[i + 1]);
			}
		}

		void AddTriMeshEdges(const CollisionProxy& p, std::vector<Edge3D>& out)
		{
			for (size_t i = 0; i + 2 < p.indices.size(); i += 3)
			{
				const auto& a = p.vertices[p.indices[i]];
				const auto& b = p.vertices[p.indices[i + 1]];
				const auto& c = p.vertices[p.indices[i + 2]];
				out.emplace_back(a, b);
				out.emplace_back(b, c);
				out.emplace_back(c, a);
			}
		}
	}

	std::vector<Edge3D> GenerateWireframeEdges(const CollisionProxy& proxy)
	{
		std::vector<Edge3D> edges;
		switch (proxy.type)
		{
			case ProxyType::Capsule:    AddCapsuleEdges(proxy, edges); break;
			case ProxyType::ConvexHull: AddHullEdges(proxy, edges);    break;
			case ProxyType::TriMesh:    AddTriMeshEdges(proxy, edges); break;
		}
		return edges;
	}
}
