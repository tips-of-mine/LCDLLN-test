#include "src/client/gameplay/CellWorldCollider.h"

#include "src/shared/world/WorldCells.h"

#include <algorithm>
#include <cmath>

namespace engine::gameplay
{
	namespace
	{
		/// Demi-côté d'une cellule (m) + marge : une primitive posée près du bord
		/// (tronc, mur) peut déborder de quelques mètres sur la voisine.
		constexpr float kCellReachMeters = static_cast<float>(engine::world::cells::kCellHalfMeters) + 64.0f;
	}

	bool CellWorldCollider::SweepCapsule(const Capsule& capsule,
		const engine::math::Vec3& startCenter,
		const engine::math::Vec3& endCenter,
		SweepHit& outHit) const
	{
		SweepHit best;
		best.hit = false;
		best.fraction = 1.0f;

		if (m_base != nullptr)
		{
			SweepHit h;
			if (m_base->SweepCapsule(capsule, startCenter, endCenter, h) && h.fraction < best.fraction)
				best = h;
		}

		const float minX = std::min(startCenter.x, endCenter.x) - capsule.radius;
		const float maxX = std::max(startCenter.x, endCenter.x) + capsule.radius;
		const float minZ = std::min(startCenter.z, endCenter.z) - capsule.radius;
		const float maxZ = std::max(startCenter.z, endCenter.z) + capsule.radius;
		for (const CellEntry& cell : m_cells)
		{
			if (cell.collider == nullptr) continue;
			if (maxX < cell.offsetX - kCellReachMeters || minX > cell.offsetX + kCellReachMeters
				|| maxZ < cell.offsetZ - kCellReachMeters || minZ > cell.offsetZ + kCellReachMeters)
				continue;
			const engine::math::Vec3 localStart{ startCenter.x - cell.offsetX, startCenter.y, startCenter.z - cell.offsetZ };
			const engine::math::Vec3 localEnd{ endCenter.x - cell.offsetX, endCenter.y, endCenter.z - cell.offsetZ };
			SweepHit h;
			// Normale et fraction sont invariantes par translation : rien à reconvertir.
			if (cell.collider->SweepCapsule(capsule, localStart, localEnd, h) && h.fraction < best.fraction)
				best = h;
		}

		outHit = best;
		return best.hit;
	}

	bool CellWorldCollider::QueryWater(const engine::math::Vec3& worldCenter, WaterQuery& out) const
	{
		if (m_base != nullptr) return m_base->QueryWater(worldCenter, out);
		out = WaterQuery{};
		return false;
	}
}
