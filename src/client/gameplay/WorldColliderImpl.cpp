// src/client/gameplay/WorldColliderImpl.cpp
#include "src/client/gameplay/WorldColliderImpl.h"
#include "src/client/world/water/WaterSampler.h"

namespace engine::gameplay
{
	void WorldColliderImpl::SetWaterSampler(
		const engine::world::water::WaterSampler* sampler) noexcept
	{
		m_waterSampler = sampler;
	}

	bool WorldColliderImpl::SweepCapsule(const Capsule& /*capsule*/,
		const engine::math::Vec3& /*startCenter*/,
		const engine::math::Vec3& /*endCenter*/,
		SweepHit& outHit) const
	{
		// Stub MVP : aucun obstacle détecté. La version complète viendra
		// avec la chaîne CHAR-MODEL (heightmap + collision proxies M100.12).
		outHit = SweepHit{};
		return false;
	}

	bool WorldColliderImpl::QueryWater(const engine::math::Vec3& worldCenter,
		WaterQuery& out) const
	{
		out = WaterQuery{};
		if (!m_waterSampler) return false;

		// Le sampler attend la position des PIEDS. Le `CharacterController`
		// nous passe la position du CENTRE de la capsule. On descend d'une
		// demi-hauteur de capsule en supposant pieds = center - height/2.
		// Comme le collider connaît la capsule via Update(), mais ici on
		// reçoit juste worldCenter, on suppose une capsule standard 1.8 m :
		// pieds = center.y - 0.9 m.
		// NOTE : si la hauteur capsule devient configurable côté Engine,
		// passer la valeur via SetCapsuleHeight() — pas nécessaire pour M100.15.
		constexpr float kAssumedHalfHeight = 0.9f;
		const engine::math::Vec3 feet{
			worldCenter.x, worldCenter.y - kAssumedHalfHeight, worldCenter.z
		};

		auto sample = m_waterSampler->Sample(feet);
		if (!sample) return false;

		out.inWater = true;
		out.surfaceY = sample->surfaceY;
		out.depth = sample->depthMeters;
		return true;
	}
}
