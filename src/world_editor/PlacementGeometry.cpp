// M100.17 — Implémentation de la géométrie de placement (pure, déterministe).

#include "src/world_editor/PlacementGeometry.h"

#include <cmath>
#include <random>

namespace engine::editor::world::placement
{
	namespace
	{
		constexpr float kPi = 3.14159265358979323846f;

		float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		Vec3 Cross(const Vec3& a, const Vec3& b)
		{
			return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
		}

		// Quaternion (x,y,z,w) depuis axe (normalisé) + angle (rad).
		void QuatAxisAngle(const Vec3& axis, float angleRad, float out[4])
		{
			const float h = angleRad * 0.5f;
			const float s = std::sin(h);
			out[0] = axis.x * s; out[1] = axis.y * s; out[2] = axis.z * s; out[3] = std::cos(h);
		}

		// q = a * b (Hamilton).
		void QuatMul(const float a[4], const float b[4], float out[4])
		{
			const float ax = a[0], ay = a[1], az = a[2], aw = a[3];
			const float bx = b[0], by = b[1], bz = b[2], bw = b[3];
			out[0] = aw * bx + ax * bw + ay * bz - az * by;
			out[1] = aw * by - ax * bz + ay * bw + az * bx;
			out[2] = aw * bz + ax * by - ay * bx + az * bw;
			out[3] = aw * bw - ax * bx - ay * by - az * bz;
		}
	} // namespace

	std::vector<Vec3> GenerateDragLine(const Vec3& start, const Vec3& end, float spacing)
	{
		std::vector<Vec3> out;
		const Vec3 d = end - start;
		const float dist = d.Length();
		if (spacing <= 0.0f || dist <= 1e-5f)
		{
			out.push_back(start);
			return out;
		}
		int segments = static_cast<int>(std::lround(dist / spacing));
		if (segments < 1) segments = 1;
		for (int i = 0; i <= segments; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(segments);
			out.push_back(Vec3(start.x + d.x * t, start.y + d.y * t, start.z + d.z * t));
		}
		return out;
	}

	std::vector<Vec3> GenerateScatter(const Vec3& center, float radius, uint32_t count, uint64_t seed)
	{
		std::vector<Vec3> out;
		out.reserve(count);
		std::mt19937_64 rng(seed);
		std::uniform_real_distribution<float> u01(0.0f, 1.0f);
		for (uint32_t i = 0; i < count; ++i)
		{
			const float r = radius * std::sqrt(u01(rng)); // distribution uniforme en surface
			const float theta = 2.0f * kPi * u01(rng);
			out.push_back(Vec3(center.x + r * std::cos(theta), center.y, center.z + r * std::sin(theta)));
		}
		return out;
	}

	YawScale RandomYawScale(uint64_t seed, float rotMinDeg, float rotMaxDeg, float scaleMin, float scaleMax)
	{
		std::mt19937_64 rng(seed);
		std::uniform_real_distribution<float> yawD(rotMinDeg, rotMaxDeg);
		std::uniform_real_distribution<float> scaleD(scaleMin, scaleMax);
		YawScale ys;
		ys.yawDeg = yawD(rng);
		ys.scale = scaleD(rng);
		return ys;
	}

	void BuildOrientation(float yawDeg, const Vec3& terrainNormal, bool alignToNormal, float outQuat[4])
	{
		// Yaw autour de Y.
		float qYaw[4];
		QuatAxisAngle(Vec3(0.0f, 1.0f, 0.0f), yawDeg * kPi / 180.0f, qYaw);

		if (!alignToNormal)
		{
			for (int i = 0; i < 4; ++i) outQuat[i] = qYaw[i];
			return;
		}

		// Rotation alignant world-up (0,1,0) sur la normale terrain.
		Vec3 n = terrainNormal.Normalized();
		const Vec3 up(0.0f, 1.0f, 0.0f);
		const float d = Dot(up, n);
		float qAlign[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		if (d < 0.9999f)
		{
			if (d < -0.9999f)
			{
				// Opposés : 180° autour de X.
				QuatAxisAngle(Vec3(1.0f, 0.0f, 0.0f), kPi, qAlign);
			}
			else
			{
				Vec3 axis = Cross(up, n).Normalized();
				const float angle = std::acos(d);
				QuatAxisAngle(axis, angle, qAlign);
			}
		}
		QuatMul(qAlign, qYaw, outQuat);
	}

	Vec3 RotateVectorByQuat(const float q[4], const Vec3& v)
	{
		// v' = v + 2*qw*(qv x v) + 2*(qv x (qv x v))
		const Vec3 qv(q[0], q[1], q[2]);
		const Vec3 t = Cross(qv, v) * 2.0f;
		return v + (t * q[3]) + Cross(qv, t);
	}
}
