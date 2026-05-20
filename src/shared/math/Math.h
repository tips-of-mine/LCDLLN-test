#pragma once

#include <cmath>
#include <cstdint>

namespace engine::math
{
	/// 3D vector (float).
	struct Vec3
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		Vec3() = default;
		Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

		Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
		Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
		Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
		Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

		/// Returns squared length (avoids sqrt).
		float LengthSq() const { return x * x + y * y + z * z; }
		float Length() const { return std::sqrt(LengthSq()); }
		/// Returns normalized vector; returns zero vector if length is zero.
		Vec3 Normalized() const
		{
			const float len = Length();
			if (len <= 0.0f) return Vec3(0.0f, 0.0f, 0.0f);
			return Vec3(x / len, y / len, z / len);
		}
	};

	inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

	/// 4x4 matrix, column-major (OpenGL/Vulkan convention).
	/// Index: m[col*4 + row], i.e. m[0..3]=col0, m[4..7]=col1, ...
	struct Mat4
	{
		float m[16] = {
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};

		Mat4() = default;

		/// Returns matrix product this * other (column-major).
		Mat4 operator*(const Mat4& other) const
		{
			Mat4 r;
			for (int col = 0; col < 4; ++col)
				for (int row = 0; row < 4; ++row)
				{
					float v = 0.0f;
					for (int i = 0; i < 4; ++i)
						v += m[i * 4 + row] * other.m[col * 4 + i];
					r.m[col * 4 + row] = v;
				}
			return r;
		}

		/// Returns the 4x4 identity matrix.
		/// Effet de bord : aucun.
		static Mat4 Identity()
		{
			return Mat4{};  // default ctor already builds identity
		}

		/// Builds a pure translation matrix (column-major).
		/// La translation vit dans la 4e colonne (m[12..14]).
		/// \param t Vecteur de translation, en unites monde (metres).
		/// Effet de bord : aucun.
		static Mat4 Translate(const Vec3& t)
		{
			Mat4 m;
			m.m[12] = t.x;
			m.m[13] = t.y;
			m.m[14] = t.z;
			return m;
		}

		/// Builds a rotation matrix around the Y axis (yaw).
		/// Convention column-major, repere main droite : RotateY(+pi/2) envoie
		/// l'axe X local vers -Z monde et l'axe Z local vers +X monde.
		/// \param radians Angle de rotation en radians (positif = sens trigo
		///                vu de +Y vers l'origine).
		/// Effet de bord : aucun.
		static Mat4 RotateY(float radians)
		{
			const float c = std::cos(radians);
			const float s = std::sin(radians);
			Mat4 m;
			m.m[0]  = c;
			m.m[2]  = -s;
			m.m[8]  = s;
			m.m[10] = c;
			return m;
		}

		/// Builds perspective matrix for Vulkan (Y down in NDC, Z in [0, 1]).
		/// \param fovYRad Vertical FOV in radians.
		/// \param aspect Width/height.
		/// \param nearZ Near plane (positive).
		/// \param farZ Far plane.
		static Mat4 PerspectiveVulkan(float fovYRad, float aspect, float nearZ, float farZ)
		{
			const float t = 1.0f / std::tan(fovYRad * 0.5f);
			const float n = nearZ;
			const float f = farZ;
			// Vulkan: Y down in NDC, Z in [0, 1], depth range 0..1
			Mat4 out;
			out.m[0] = t / aspect; out.m[1] = 0.0f; out.m[2] = 0.0f; out.m[3] = 0.0f;
			out.m[4] = 0.0f; out.m[5] = -t; out.m[6] = 0.0f; out.m[7] = 0.0f;
			out.m[8] = 0.0f; out.m[9] = 0.0f; out.m[10] = f / (f - n); out.m[11] = 1.0f;
			out.m[12] = 0.0f; out.m[13] = 0.0f; out.m[14] = -n * f / (f - n); out.m[15] = 0.0f;
			return out;
		}
	};
}
