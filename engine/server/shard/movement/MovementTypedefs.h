#pragma once
// CMANGOS.04 (Phase 2.04a) — MovementTypedefs : centralisation des
// types vectoriels utilises par le sous-systeme `MoveSpline`. On
// reutilise `engine::math::Vec3` existant + on ajoute `Vec4` et `Quat`
// minimaux. Pas de dependance externe (math header-only).
//
// Quaternion : utilise pour l'orientation (yaw/pitch/roll) le long
// d'une spline. Implementation minimale (somme + produit scalaire,
// pas de slerp dans cette PR — viendra avec MoveSpline runtime).

#include "engine/math/Math.h"

#include <cmath>

namespace engine::server::shard::movement
{
	using Vec3 = engine::math::Vec3;

	/// Vec4 (xyz + w). Pas de namespace partage avec engine::math pour
	/// rester local au sous-systeme movement (audit §1 : on ne veut PAS
	/// pousser un Vec4 generique avant d'avoir un usage cote client).
	struct Vec4
	{
		float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;

		constexpr Vec4() = default;
		constexpr Vec4(float xv, float yv, float zv, float wv) noexcept
			: x(xv), y(yv), z(zv), w(wv) {}

		constexpr Vec4 operator+(const Vec4& o) const noexcept
		{
			return Vec4(x + o.x, y + o.y, z + o.z, w + o.w);
		}
		constexpr Vec4 operator-(const Vec4& o) const noexcept
		{
			return Vec4(x - o.x, y - o.y, z - o.z, w - o.w);
		}
		constexpr Vec4 operator*(float s) const noexcept
		{
			return Vec4(x * s, y * s, z * s, w * s);
		}

		constexpr float Dot(const Vec4& o) const noexcept
		{
			return x * o.x + y * o.y + z * o.z + w * o.w;
		}
		float Length() const noexcept { return std::sqrt(Dot(*this)); }
	};

	constexpr Vec4 operator*(float s, const Vec4& v) noexcept { return v * s; }

	/// Quaternion (qx, qy, qz, qw). Convention {x,y,z}=axis*sin(angle/2),
	/// w=cos(angle/2). Identite = (0,0,0,1).
	struct Quat
	{
		float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

		constexpr Quat() = default;
		constexpr Quat(float xv, float yv, float zv, float wv) noexcept
			: x(xv), y(yv), z(zv), w(wv) {}

		static constexpr Quat Identity() noexcept { return Quat(0, 0, 0, 1); }

		/// Normalise le quaternion. No-op si len = 0.
		Quat Normalized() const noexcept
		{
			const float len2 = x * x + y * y + z * z + w * w;
			if (len2 <= 0.0f) return Identity();
			const float invLen = 1.0f / std::sqrt(len2);
			return Quat(x * invLen, y * invLen, z * invLen, w * invLen);
		}

		/// Construit un quaternion a partir d'un yaw (rotation autour
		/// de l'axe vertical Y, en radians). Convention "yaw" cmangos.
		static Quat FromYaw(float yawRad) noexcept
		{
			const float h = yawRad * 0.5f;
			return Quat(0.0f, std::sin(h), 0.0f, std::cos(h));
		}
	};
}
