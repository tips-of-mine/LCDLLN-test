#include "engine/math/Frustum.h"
#include <cmath>
#include <algorithm>

namespace engine::math
{
	namespace
	{
		/// Column-major: column col has components m[col*4+0..3].
		/// Row row has components m[0*4+row], m[1*4+row], m[2*4+row], m[3*4+row].
		inline void getRow(const Mat4& M, int row, float& x, float& y, float& z, float& w)
		{
			x = M.m[0 * 4 + row];
			y = M.m[1 * 4 + row];
			z = M.m[2 * 4 + row];
			w = M.m[3 * 4 + row];
		}

		Plane normalizePlane(float nx, float ny, float nz, float d)
		{
			const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (len <= 0.0f) return Plane{ 0.0f, 0.0f, 0.0f, 0.0f };
			const float inv = 1.0f / len;
			return Plane{ nx * inv, ny * inv, nz * inv, d * inv };
		}
	}

	void Frustum::ExtractFromMatrix(const Mat4& viewProj)
	{
		float r0x, r0y, r0z, r0w;
		float r1x, r1y, r1z, r1w;
		float r2x, r2y, r2z, r2w;
		float r3x, r3y, r3z, r3w;
		getRow(viewProj, 0, r0x, r0y, r0z, r0w);
		getRow(viewProj, 1, r1x, r1y, r1z, r1w);
		getRow(viewProj, 2, r2x, r2y, r2z, r2w);
		getRow(viewProj, 3, r3x, r3y, r3z, r3w);

		m_planes[static_cast<size_t>(PlaneIndex::Left)]   = normalizePlane(r3x + r0x, r3y + r0y, r3z + r0z, r3w + r0w);
		m_planes[static_cast<size_t>(PlaneIndex::Right)]  = normalizePlane(r3x - r0x, r3y - r0y, r3z - r0z, r3w - r0w);
		m_planes[static_cast<size_t>(PlaneIndex::Bottom)] = normalizePlane(r3x + r1x, r3y + r1y, r3z + r1z, r3w + r1w);
		m_planes[static_cast<size_t>(PlaneIndex::Top)]    = normalizePlane(r3x - r1x, r3y - r1y, r3z - r1z, r3w - r1w);
		m_planes[static_cast<size_t>(PlaneIndex::Near)]    = normalizePlane(r3x + r2x, r3y + r2y, r3z + r2z, r3w + r2w);
		m_planes[static_cast<size_t>(PlaneIndex::Far)]     = normalizePlane(r3x - r2x, r3y - r2y, r3z - r2z, r3w - r2w);
	}

	bool Frustum::TestAABB(const Vec3& minBounds, const Vec3& maxBounds) const
	{
		for (size_t i = 0; i < 6; ++i)
		{
			const Plane& p = m_planes[i];
			// Positive vertex: the corner of AABB most in direction of plane normal.
			const float px = p.nx >= 0.0f ? maxBounds.x : minBounds.x;
			const float py = p.ny >= 0.0f ? maxBounds.y : minBounds.y;
			const float pz = p.nz >= 0.0f ? maxBounds.z : minBounds.z;
			const float dist = p.nx * px + p.ny * py + p.nz * pz + p.d;
			if (dist < 0.0f)
				return false; // fully outside this plane
		}
		return true;
	}

	const Plane& Frustum::GetPlane(PlaneIndex i) const
	{
		return m_planes[static_cast<size_t>(i)];
	}

	Plane& Frustum::GetPlane(PlaneIndex i)
	{
		return m_planes[static_cast<size_t>(i)];
	}
}
