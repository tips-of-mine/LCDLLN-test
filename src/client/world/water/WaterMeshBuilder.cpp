// src/client/world/water/WaterMeshBuilder.cpp
#include "src/client/world/water/WaterMeshBuilder.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace engine::world::water
{
	namespace
	{
		/// Aire signée 2D du polygone (XZ). Positive si CCW.
		float SignedArea2D(const std::vector<engine::math::Vec3>& poly)
		{
			float a = 0.0f;
			for (size_t i = 0; i < poly.size(); ++i)
			{
				const auto& cur  = poly[i];
				const auto& next = poly[(i + 1) % poly.size()];
				a += (cur.x * next.z - next.x * cur.z);
			}
			return a * 0.5f;
		}

		/// True si l'angle au coin (a, b, c) est convexe (CCW polygon).
		/// Cross product 2D (XZ) du segment ab et bc > 0.
		bool IsConvexCorner(const engine::math::Vec3& a,
		                    const engine::math::Vec3& b,
		                    const engine::math::Vec3& c)
		{
			const float ux = b.x - a.x, uz = b.z - a.z;
			const float vx = c.x - b.x, vz = c.z - b.z;
			return (ux * vz - uz * vx) > 0.0f;
		}

		/// True si le point p est dans le triangle (a, b, c) dans XZ.
		bool PointInTriangleXZ(const engine::math::Vec3& p,
		                       const engine::math::Vec3& a,
		                       const engine::math::Vec3& b,
		                       const engine::math::Vec3& c)
		{
			const float d1 = (p.x - b.x) * (a.z - b.z) - (a.x - b.x) * (p.z - b.z);
			const float d2 = (p.x - c.x) * (b.z - c.z) - (b.x - c.x) * (p.z - c.z);
			const float d3 = (p.x - a.x) * (c.z - a.z) - (c.x - a.x) * (p.z - a.z);
			const bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
			const bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
			return !(hasNeg && hasPos);
		}

		/// Ear clipping helper : test si un vertex restant (autre que prev/curr/next)
		/// est dans le triangle.
		bool TriangleContainsAnyOther(const std::vector<uint32_t>& remaining,
		                              size_t earIdx,
		                              const std::vector<engine::math::Vec3>& poly)
		{
			const size_t n = remaining.size();
			const uint32_t prev = remaining[(earIdx + n - 1) % n];
			const uint32_t curr = remaining[earIdx];
			const uint32_t next = remaining[(earIdx + 1) % n];
			for (size_t i = 0; i < n; ++i)
			{
				const uint32_t v = remaining[i];
				if (v == prev || v == curr || v == next) continue;
				if (PointInTriangleXZ(poly[v], poly[prev], poly[curr], poly[next]))
					return true;
			}
			return false;
		}
	}

	bool BuildLakeMesh(const LakeInstance& lake,
		WaterMeshCpu& out, std::string& err)
	{
		out.vertices.clear();
		out.indices.clear();

		if (lake.polygon.size() < 3)
		{
			err = "BuildLakeMesh: polygon needs >= 3 vertices";
			return false;
		}

		// Force CCW
		std::vector<engine::math::Vec3> poly = lake.polygon;
		if (SignedArea2D(poly) < 0.0f)
			std::reverse(poly.begin(), poly.end());

		// Indices restants à découper
		std::vector<uint32_t> remaining(poly.size());
		std::iota(remaining.begin(), remaining.end(), 0u);

		while (remaining.size() > 3)
		{
			bool foundEar = false;
			for (size_t i = 0; i < remaining.size(); ++i)
			{
				const size_t n = remaining.size();
				const uint32_t prev = remaining[(i + n - 1) % n];
				const uint32_t curr = remaining[i];
				const uint32_t next = remaining[(i + 1) % n];
				if (!IsConvexCorner(poly[prev], poly[curr], poly[next])) continue;
				if (TriangleContainsAnyOther(remaining, i, poly)) continue;
				out.indices.push_back(prev);
				out.indices.push_back(curr);
				out.indices.push_back(next);
				remaining.erase(remaining.begin() + i);
				foundEar = true;
				break;
			}
			if (!foundEar)
			{
				err = "BuildLakeMesh: ear-clipping failed (auto-intersecting polygon?)";
				return false;
			}
		}
		// Dernier triangle
		out.indices.push_back(remaining[0]);
		out.indices.push_back(remaining[1]);
		out.indices.push_back(remaining[2]);

		// Vertices : positions XZ du polygone, Y = waterLevelY
		out.vertices.reserve(poly.size());
		for (const auto& p : poly)
			out.vertices.push_back({ engine::math::Vec3{ p.x, lake.waterLevelY, p.z } });

		return true;
	}

	bool BuildRiverMesh(const RiverInstance& river,
		WaterMeshCpu& out, std::string& err)
	{
		out.vertices.clear();
		out.indices.clear();

		if (river.nodes.size() < 2)
		{
			err = "BuildRiverMesh: river needs >= 2 nodes";
			return false;
		}

		out.vertices.reserve(2 * river.nodes.size());
		out.indices.reserve(6 * (river.nodes.size() - 1));

		for (size_t i = 0; i < river.nodes.size(); ++i)
		{
			engine::math::Vec3 tangent;
			if (i == 0)
				tangent = river.nodes[1].position - river.nodes[0].position;
			else if (i == river.nodes.size() - 1)
				tangent = river.nodes[i].position - river.nodes[i - 1].position;
			else
				tangent = river.nodes[i + 1].position - river.nodes[i - 1].position;

			const float tlen = std::sqrt(tangent.x * tangent.x + tangent.z * tangent.z);
			const float perpX = (tlen > 0.0f) ? (-tangent.z / tlen) : 1.0f;
			const float perpZ = (tlen > 0.0f) ? ( tangent.x / tlen) : 0.0f;
			const float halfW = river.nodes[i].widthMeters * 0.5f;

			const auto& n = river.nodes[i];
			out.vertices.push_back({ engine::math::Vec3{
				n.position.x + perpX * halfW, n.position.y, n.position.z + perpZ * halfW } });
			out.vertices.push_back({ engine::math::Vec3{
				n.position.x - perpX * halfW, n.position.y, n.position.z - perpZ * halfW } });
		}

		for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(river.nodes.size()); ++i)
		{
			const uint32_t a = i * 2 + 0;
			const uint32_t b = i * 2 + 1;
			const uint32_t c = (i + 1) * 2 + 0;
			const uint32_t d = (i + 1) * 2 + 1;
			out.indices.push_back(a); out.indices.push_back(c); out.indices.push_back(d);
			out.indices.push_back(a); out.indices.push_back(d); out.indices.push_back(b);
		}
		return true;
	}

	std::vector<engine::math::Vec3> ComputeFlowDirections(const RiverInstance& river)
	{
		std::vector<engine::math::Vec3> flows;
		if (river.nodes.size() < 2) return flows;
		flows.reserve(river.nodes.size() - 1);
		for (size_t i = 0; i + 1 < river.nodes.size(); ++i)
		{
			const float dx = river.nodes[i + 1].position.x - river.nodes[i].position.x;
			const float dz = river.nodes[i + 1].position.z - river.nodes[i].position.z;
			const float len = std::sqrt(dx * dx + dz * dz);
			if (len > 0.0f)
				flows.push_back({ dx / len, 0.0f, dz / len });
			else
				flows.push_back({ 1.0f, 0.0f, 0.0f });
		}
		return flows;
	}
}
