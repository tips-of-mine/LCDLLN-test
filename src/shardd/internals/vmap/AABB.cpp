#include "src/shardd/internals/vmap/AABB.h"

namespace engine::server::shard::vmap
{
	namespace
	{
		/// Slab test sur un axe : retourne true si le rayon coupe la slab
		/// [bMin, bMax] sur cet axe ; met à jour [tNear, tFar] cumulés.
		///
		/// \param o  Coord origin sur l'axe.
		/// \param d  Coord direction sur l'axe.
		/// \param bMin / bMax  Bornes de la slab.
		/// \param tNear / tFar  [in/out] intervalle cumulé des intersections.
		inline bool SlabTest(float o, float d, float bMin, float bMax,
			float& tNear, float& tFar) noexcept
		{
			constexpr float kEps = 1e-9f;
			if (std::fabs(d) < kEps)
			{
				// Rayon parallèle à la slab : pas d'intersection sauf si
				// l'origine est déjà dedans.
				return o >= bMin && o <= bMax;
			}
			const float invD = 1.0f / d;
			float t1 = (bMin - o) * invD;
			float t2 = (bMax - o) * invD;
			if (t1 > t2) std::swap(t1, t2);
			if (t1 > tNear) tNear = t1;
			if (t2 < tFar)  tFar  = t2;
			return tNear <= tFar;
		}
	}

	bool IntersectRayAABB(const Ray& r, const AABB& box, float& tHitOut) noexcept
	{
		float tNear = r.tMin;
		float tFar  = r.tMax;
		if (!SlabTest(r.origin.x, r.dir.x, box.min.x, box.max.x, tNear, tFar))
			return false;
		if (!SlabTest(r.origin.y, r.dir.y, box.min.y, box.max.y, tNear, tFar))
			return false;
		if (!SlabTest(r.origin.z, r.dir.z, box.min.z, box.max.z, tNear, tFar))
			return false;
		// Si tNear < r.tMin, l'origine du rayon est dans la boîte —
		// retourner r.tMin (entrée du segment paramétrique). Sinon tNear
		// est l'instant d'entrée dans la boîte.
		tHitOut = (tNear < r.tMin) ? r.tMin : tNear;
		return true;
	}
}
