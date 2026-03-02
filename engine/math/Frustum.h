#pragma once

#include "engine/math/Math.h"
#include <cstdint>

namespace engine::math
{
	/// Plane equation: n.x * x + n.y * y + n.z * z + d = 0 (normal n, signed distance d).
	struct Plane
	{
		float nx = 0.0f;
		float ny = 0.0f;
		float nz = 0.0f;
		float d = 0.0f;
	};

	/// Frustum: 6 planes (left, right, top, bottom, near, far).
	/// Used for visibility culling (e.g. AABB vs frustum).
	class Frustum
	{
	public:
		enum class PlaneIndex : uint8_t
		{
			Left = 0,
			Right,
			Top,
			Bottom,
			Near,
			Far,
			Count = 6
		};

		/// Extracts the 6 planes from the combined view-projection matrix (column-major).
		/// Planes are normalized (unit-length normal).
		void ExtractFromMatrix(const Mat4& viewProj);

		/// Tests an AABB against the frustum.
		/// \return true if the AABB is inside or intersecting the frustum; false if fully outside.
		bool TestAABB(const Vec3& minBounds, const Vec3& maxBounds) const;

		const Plane& GetPlane(PlaneIndex i) const;
		Plane& GetPlane(PlaneIndex i);

	private:
		Plane m_planes[6] = {};
	};
}
