// M100.30 — Implémentation des helpers de structures (purs).

#include "src/client/world/structures/Structures.h"

#include <algorithm>
#include <cmath>

namespace engine::world::structures
{
	using engine::math::Vec3;

	float PolylineLength(const std::vector<Vec3>& pts)
	{
		float total = 0.0f;
		for (size_t i = 1; i < pts.size(); ++i)
		{
			const float dx = pts[i].x - pts[i-1].x, dy = pts[i].y - pts[i-1].y, dz = pts[i].z - pts[i-1].z;
			total += std::sqrt(dx*dx + dy*dy + dz*dz);
		}
		return total;
	}

	int BridgeSegmentCount(float lengthMeters, float segmentLengthMeters)
	{
		if (segmentLengthMeters <= 1e-5f || lengthMeters <= 0.0f) return 1;
		int n = static_cast<int>(std::lround(lengthMeters / segmentLengthMeters));
		return std::max(1, n);
	}

	std::vector<float> WallPostDistances(float lengthMeters, float postSpacingMeters)
	{
		std::vector<float> out;
		if (postSpacingMeters <= 1e-5f || lengthMeters < 0.0f) { out.push_back(0.0f); return out; }
		for (float d = 0.0f; d <= lengthMeters + 1e-4f; d += postSpacingMeters) out.push_back(d);
		// Garantit un poteau de terminaison exact à la fin.
		if (out.empty() || std::fabs(out.back() - lengthMeters) > 1e-3f) out.push_back(lengthMeters);
		return out;
	}

	std::vector<int> DetectSharpCorners(const std::vector<Vec3>& nodes, float thresholdDeg)
	{
		std::vector<int> corners;
		constexpr float kRadToDeg = 57.2957795f;
		for (size_t i = 1; i + 1 < nodes.size(); ++i)
		{
			const Vec3 d0(nodes[i].x - nodes[i-1].x, 0.0f, nodes[i].z - nodes[i-1].z);
			const Vec3 d1(nodes[i+1].x - nodes[i].x, 0.0f, nodes[i+1].z - nodes[i].z);
			const float l0 = std::sqrt(d0.x*d0.x + d0.z*d0.z), l1 = std::sqrt(d1.x*d1.x + d1.z*d1.z);
			if (l0 < 1e-5f || l1 < 1e-5f) continue;
			float cosA = (d0.x*d1.x + d0.z*d1.z) / (l0 * l1);
			cosA = std::max(-1.0f, std::min(1.0f, cosA));
			const float turnDeg = std::acos(cosA) * kRadToDeg; // 0 = tout droit
			if (turnDeg >= thresholdDeg) corners.push_back(static_cast<int>(i));
		}
		return corners;
	}

	bool QueryBridgeSurface(const BridgeWalkable& bridge, const Vec3& pos)
	{
		const float dx = bridge.b.x - bridge.a.x, dz = bridge.b.z - bridge.a.z;
		const float len2 = dx*dx + dz*dz;
		float t = (len2 > 1e-9f) ? ((pos.x - bridge.a.x) * dx + (pos.z - bridge.a.z) * dz) / len2 : 0.0f;
		t = std::max(0.0f, std::min(1.0f, t));
		const float cx = bridge.a.x + t * dx, cz = bridge.a.z + t * dz;
		const float ex = pos.x - cx, ez = pos.z - cz;
		const float lateral = std::sqrt(ex*ex + ez*ez);
		if (lateral > bridge.widthMeters * 0.5f) return false;
		return std::fabs(pos.y - bridge.bridgeY) <= 0.3f;
	}
}
