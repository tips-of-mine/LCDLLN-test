#include "src/shardd/internals/vmap/VMapManager.h"

#include <cmath>
#include <limits>

namespace engine::server::shard::vmap
{
	bool VMapManager::LoadTile(std::span<const uint8_t> blob)
	{
		VMapTile tile;
		if (DecodeVMapTile(blob, tile) != VMapDecodeResult::OK)
			return false;
		LoadTileDecoded(std::move(tile));
		return true;
	}

	void VMapManager::LoadTileDecoded(VMapTile tile)
	{
		Clear();

		// Construire la liste des triangles "objet" pour la BIH.
		m_triangles.reserve(tile.triangles.size());
		for (const auto& t : tile.triangles)
		{
			VMapTriObject obj;
			obj.v0 = tile.vertices[t.a];
			obj.v1 = tile.vertices[t.b];
			obj.v2 = tile.vertices[t.c];
			obj.bounds = AABB::Empty();
			obj.bounds.Expand(obj.v0);
			obj.bounds.Expand(obj.v1);
			obj.bounds.Expand(obj.v2);
			m_triangles.push_back(obj);
		}

		m_bbox = tile.bbox;
		m_bih.Build(m_triangles);
	}

	void VMapManager::Clear()
	{
		m_triangles.clear();
		m_bih.Clear();
		m_bbox = AABB::Empty();
	}

	bool VMapManager::IsInLineOfSight(const Vec3& p1, const Vec3& p2) const
	{
		if (!IsLoaded())
			return true;  // pas d'obstruction connue

		const Vec3 dir = p2 - p1;
		const float segLen = dir.Length();
		if (segLen <= 0.0f)
			return true;  // p1 == p2 → trivialement visible

		Ray r(p1, dir);
		r.tMin = 0.0f;
		r.tMax = 1.0f;  // segment paramétrique [0,1] : on s'arrete a p2

		auto hitTest = [&](uint32_t idx, const Ray& ray, float& tOut) {
			return IntersectRayTri(ray, m_triangles[idx], tOut);
		};

		const float tHit = m_bih.Raycast(r, m_triangles, hitTest);
		// Si tHit ∈ [0, 1], il y a un triangle entre p1 et p2 → bloque.
		return tHit > 1.0f || std::isinf(tHit);
	}

	std::optional<float> VMapManager::GetHeight(float x, float z,
		float maxSearchHeight) const
	{
		if (!IsLoaded())
			return std::nullopt;

		// Raycast vertical descendant : origine = (x, maxY, z), dir = -Y.
		const Vec3 origin(x, maxSearchHeight, z);
		Ray r(origin, Vec3(0.0f, -1.0f, 0.0f));
		r.tMin = 0.0f;
		r.tMax = std::numeric_limits<float>::infinity();

		auto hitTest = [&](uint32_t idx, const Ray& ray, float& tOut) {
			return IntersectRayTri(ray, m_triangles[idx], tOut);
		};

		const float tHit = m_bih.Raycast(r, m_triangles, hitTest);
		if (std::isinf(tHit))
			return std::nullopt;

		// Point d'impact sur l'axe Y : origine.y - tHit.
		return maxSearchHeight - tHit;
	}

	bool VMapManager::IntersectRayTri(const Ray& r, const VMapTriObject& tri,
		float& tHit) noexcept
	{
		// Möller-Trumbore. Reference :
		// https://en.wikipedia.org/wiki/M%C3%B6ller%E2%80%93Trumbore_intersection_algorithm
		constexpr float kEps = 1e-7f;

		const Vec3 e1 = tri.v1 - tri.v0;
		const Vec3 e2 = tri.v2 - tri.v0;

		// Pvec = ray.dir × e2
		const Vec3 pvec(
			r.dir.y * e2.z - r.dir.z * e2.y,
			r.dir.z * e2.x - r.dir.x * e2.z,
			r.dir.x * e2.y - r.dir.y * e2.x);

		const float det = e1.x * pvec.x + e1.y * pvec.y + e1.z * pvec.z;
		if (std::fabs(det) < kEps)
			return false;  // rayon parallèle au triangle
		const float invDet = 1.0f / det;

		const Vec3 tvec = r.origin - tri.v0;
		const float u = (tvec.x * pvec.x + tvec.y * pvec.y + tvec.z * pvec.z) * invDet;
		if (u < 0.0f || u > 1.0f)
			return false;

		// qvec = tvec × e1
		const Vec3 qvec(
			tvec.y * e1.z - tvec.z * e1.y,
			tvec.z * e1.x - tvec.x * e1.z,
			tvec.x * e1.y - tvec.y * e1.x);

		const float v = (r.dir.x * qvec.x + r.dir.y * qvec.y + r.dir.z * qvec.z) * invDet;
		if (v < 0.0f || u + v > 1.0f)
			return false;

		const float t = (e2.x * qvec.x + e2.y * qvec.y + e2.z * qvec.z) * invDet;
		if (t < r.tMin || t > r.tMax)
			return false;

		tHit = t;
		return true;
	}
}
