#include "src/client/world/scatter/ScatterField.h"

#include <algorithm>
#include <cmath>

namespace engine::world::scatter
{
	namespace
	{
		int32_t CellOf(float v) { return static_cast<int32_t>(std::floor(v / ScatterField::kCellM)); }
	}

	Frustum Frustum::FromViewProj(const float* m)
	{
		// m column-major : élément (ligne r, colonne c) = m[c*4 + r].
		auto row = [m](int r) { return std::array<float, 4>{ m[0 * 4 + r], m[1 * 4 + r], m[2 * 4 + r], m[3 * 4 + r] }; };
		const auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
		Frustum f;
		auto set = [&f](int i, float a, float b, float c, float d) {
			const float n = std::sqrt(a * a + b * b + c * c);
			f.planes[static_cast<size_t>(i)] = (n > 1e-8f) ? std::array<float, 4>{ a / n, b / n, c / n, d / n }
			                                                : std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f };
		};
		set(0, r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]); // gauche
		set(1, r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]); // droite
		set(2, r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]); // bas/haut (Y inversé Vulkan : symétrique)
		set(3, r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]);
		set(4, r2[0], r2[1], r2[2], r2[3]);                                  // proche (Z ≥ 0, Vulkan)
		set(5, r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]); // lointain
		return f;
	}

	bool Frustum::SphereVisible(const engine::math::Vec3& c, float r) const
	{
		for (const auto& p : planes)
			if (p[0] * c.x + p[1] * c.y + p[2] * c.z + p[3] < -r) return false;
		return true;
	}

	void ScatterField::Clear()
	{
		m_items.clear();
		m_meshPaths.clear();
		m_meshIndex.clear();
		m_grid.clear();
		m_gridDirty = true;
		m_maxRadius = 0.0f;
	}

	uint32_t ScatterField::InternMesh(const std::string& meshPath)
	{
		auto it = m_meshIndex.find(meshPath);
		if (it != m_meshIndex.end()) return it->second;
		const uint32_t id = static_cast<uint32_t>(m_meshPaths.size());
		m_meshPaths.push_back(meshPath);
		m_meshIndex.emplace(meshPath, id);
		return id;
	}

	uint32_t ScatterField::Add(const ScatterItem& item)
	{
		m_items.push_back(item);
		m_gridDirty = true;
		return static_cast<uint32_t>(m_items.size() - 1);
	}

	void ScatterField::RebuildGrid() const
	{
		m_grid.clear();
		m_maxRadius = 0.0f;
		for (uint32_t i = 0; i < static_cast<uint32_t>(m_items.size()); ++i)
		{
			const ScatterItem& it = m_items[i];
			m_grid[Key(CellOf(it.center.x), CellOf(it.center.z))].push_back(i);
			m_maxRadius = std::max(m_maxRadius, it.radius);
		}
		m_gridDirty = false;
	}

	void ScatterField::Gather(const engine::math::Vec3& eye, const Frustum& frustum,
		const CullDistances& dist, std::vector<uint32_t>& out) const
	{
		out.clear();
		if (m_items.empty()) return;
		if (m_gridDirty) RebuildGrid();
		// Les items sont indexés par la cellule de leur CENTRE : on élargit la fenêtre
		// du plus grand rayon (borné à 256 m) pour ne pas rater un long rempart.
		const float maxD = std::max({ dist.largeM, dist.mediumM, dist.smallM }) + std::min(m_maxRadius, 256.0f);
		const int32_t x0 = CellOf(eye.x - maxD), x1 = CellOf(eye.x + maxD);
		const int32_t z0 = CellOf(eye.z - maxD), z1 = CellOf(eye.z + maxD);
		for (int32_t cz = z0; cz <= z1; ++cz)
			for (int32_t cx = x0; cx <= x1; ++cx)
			{
				auto it = m_grid.find(Key(cx, cz));
				if (it == m_grid.end()) continue;
				for (uint32_t idx : it->second)
				{
					const ScatterItem& s = m_items[idx];
					const float dx = s.center.x - eye.x, dz = s.center.z - eye.z;
					// Distance au BORD de la sphère : un grand bâtiment reste visible
					// tant qu'une partie est dans le rayon.
					const float lim = dist.For(s.cullClass) + s.radius;
					if (dx * dx + dz * dz > lim * lim) continue;
					if (!frustum.SphereVisible(s.center, s.radius)) continue;
					out.push_back(idx);
				}
			}
	}

	void ScatterField::GatherNearSegment(const engine::math::Vec3& a, const engine::math::Vec3& b,
		float margin, std::vector<uint32_t>& out) const
	{
		out.clear();
		if (m_items.empty()) return;
		if (m_gridDirty) RebuildGrid();
		const float pad = margin + std::min(m_maxRadius, 256.0f);
		const int32_t x0 = CellOf(std::min(a.x, b.x) - pad), x1 = CellOf(std::max(a.x, b.x) + pad);
		const int32_t z0 = CellOf(std::min(a.z, b.z) - pad), z1 = CellOf(std::max(a.z, b.z) + pad);
		const float abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
		const float len2 = abx * abx + aby * aby + abz * abz;
		for (int32_t cz = z0; cz <= z1; ++cz)
			for (int32_t cx = x0; cx <= x1; ++cx)
			{
				auto it = m_grid.find(Key(cx, cz));
				if (it == m_grid.end()) continue;
				for (uint32_t idx : it->second)
				{
					const ScatterItem& s = m_items[idx];
					if ((s.flags & 4u) == 0u) continue;
					const float px = s.center.x - a.x, py = s.center.y - a.y, pz = s.center.z - a.z;
					float t = len2 > 1e-8f ? (px * abx + py * aby + pz * abz) / len2 : 0.0f;
					t = std::clamp(t, 0.0f, 1.0f);
					const float qx = px - abx * t, qy = py - aby * t, qz = pz - abz * t;
					const float lim = s.radius + margin;
					if (qx * qx + qy * qy + qz * qz <= lim * lim) out.push_back(idx);
				}
			}
	}

	engine::math::Mat4 ComposeInstanceMatrix(const engine::math::Vec3& position,
		float yawDeg, float tiltDeg, float scale)
	{
		constexpr float kDeg2Rad = 3.14159265358979f / 180.0f;
		engine::math::Mat4 s = engine::math::Mat4::Identity();
		s.m[0] = scale; s.m[5] = scale; s.m[10] = scale;
		return engine::math::Mat4::Translate(position) *
			engine::math::Mat4::RotateY(yawDeg * kDeg2Rad) *
			engine::math::Mat4::RotateX(tiltDeg * kDeg2Rad) * s;
	}

	void TransformBounds(const engine::math::Mat4& m, const engine::math::Vec3& lo,
		const engine::math::Vec3& hi, engine::math::Vec3& outCenter, float& outRadius,
		engine::math::Vec3& outMin, engine::math::Vec3& outMax)
	{
		outMin = { 1e30f, 1e30f, 1e30f };
		outMax = { -1e30f, -1e30f, -1e30f };
		for (int k = 0; k < 8; ++k)
		{
			const float x = (k & 1) ? hi.x : lo.x, y = (k & 2) ? hi.y : lo.y, z = (k & 4) ? hi.z : lo.z;
			const float wx = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
			const float wy = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
			const float wz = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
			outMin = { std::min(outMin.x, wx), std::min(outMin.y, wy), std::min(outMin.z, wz) };
			outMax = { std::max(outMax.x, wx), std::max(outMax.y, wy), std::max(outMax.z, wz) };
		}
		outCenter = { 0.5f * (outMin.x + outMax.x), 0.5f * (outMin.y + outMax.y), 0.5f * (outMin.z + outMax.z) };
		const float ex = outMax.x - outMin.x, ey = outMax.y - outMin.y, ez = outMax.z - outMin.z;
		outRadius = 0.5f * std::sqrt(ex * ex + ey * ey + ez * ez);
		if (outRadius < 0.05f) outRadius = 0.05f;
	}
}
