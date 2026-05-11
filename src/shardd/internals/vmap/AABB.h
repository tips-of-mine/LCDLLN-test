#pragma once
// CMANGOS.05 (Phase 2.05a) — AABB + Ray + intersection tests, brique de
// base pour la BIH (Bounding Interval Hierarchy) qui sera la structure
// d'accélération du vmap server-side (LOS / GetHeight / projectile).
//
// Pure math, pas de dépendance externe au-delà de src/shared/math/Math.h
// (Vec3). Testable en isolation.

#include "src/shared/math/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::server::shard::vmap
{
	using engine::math::Vec3;

	/// Axis-Aligned Bounding Box. \p min ≤ \p max sur chaque axe.
	struct AABB
	{
		Vec3 min{};
		Vec3 max{};

		AABB() = default;
		AABB(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}

		/// Construit la "boîte vide" (min = +inf, max = -inf). Utile pour
		/// `Expand(point)` itératif en partant de zéro.
		static AABB Empty() noexcept
		{
			constexpr float inf = std::numeric_limits<float>::infinity();
			return AABB(Vec3(+inf, +inf, +inf), Vec3(-inf, -inf, -inf));
		}

		bool IsValid() const noexcept
		{
			return min.x <= max.x && min.y <= max.y && min.z <= max.z;
		}

		/// True si le point \p p est strictement à l'intérieur (frontière incluse).
		bool Contains(const Vec3& p) const noexcept
		{
			return p.x >= min.x && p.x <= max.x
				&& p.y >= min.y && p.y <= max.y
				&& p.z >= min.z && p.z <= max.z;
		}

		/// Étend la boîte pour contenir \p p (no-op si déjà dedans).
		void Expand(const Vec3& p) noexcept
		{
			if (p.x < min.x) min.x = p.x;
			if (p.y < min.y) min.y = p.y;
			if (p.z < min.z) min.z = p.z;
			if (p.x > max.x) max.x = p.x;
			if (p.y > max.y) max.y = p.y;
			if (p.z > max.z) max.z = p.z;
		}

		/// Étend pour contenir une autre AABB.
		void Expand(const AABB& other) noexcept
		{
			Expand(other.min);
			Expand(other.max);
		}

		/// Centre géométrique.
		Vec3 Center() const noexcept
		{
			return Vec3(0.5f * (min.x + max.x),
				0.5f * (min.y + max.y),
				0.5f * (min.z + max.z));
		}

		/// Demi-extent (taille / 2).
		Vec3 HalfSize() const noexcept
		{
			return Vec3(0.5f * (max.x - min.x),
				0.5f * (max.y - min.y),
				0.5f * (max.z - min.z));
		}
	};

	/// Rayon paramétrique : `P(t) = origin + t * dir`, t ∈ [tMin, tMax].
	/// `dir` n'a pas besoin d'être normalisé : les distances retournées
	/// sont en unités de `dir`. Pour avoir des mètres, normaliser \p dir.
	struct Ray
	{
		Vec3  origin{};
		Vec3  dir{};
		float tMin = 0.0f;
		float tMax = std::numeric_limits<float>::infinity();

		Ray() = default;
		Ray(const Vec3& o, const Vec3& d, float tn = 0.0f,
			float tx = std::numeric_limits<float>::infinity())
			: origin(o), dir(d), tMin(tn), tMax(tx) {}
	};

	/// Test rayon ↔ AABB (slab method). Retourne true si le rayon coupe
	/// la boîte dans [tMin, tMax]. Met \p tHitOut au paramètre d'entrée
	/// (peut être négatif si l'origine est dans la boîte).
	///
	/// **Cas particulier** : si `dir.{x,y,z}` est ≈ 0 et `origin` n'est
	/// pas dans la slab correspondante, on retourne false. Si dans la
	/// slab, on traite l'axe comme "toujours dans l'intervalle".
	///
	/// Algorithme classique en O(1), sans branchement majeur, sûr pour
	/// les rayons d'origine intérieure.
	bool IntersectRayAABB(const Ray& r, const AABB& box, float& tHitOut) noexcept;
}
