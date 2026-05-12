// src/client/gameplay/WorldColliderImpl.h
#pragma once

#include "src/client/gameplay/CharacterController.h"

namespace engine::world::water { class WaterSampler; }

namespace engine::gameplay
{
	/// Première implémentation concrète d'`IWorldCollider`. Pour M100.15 :
	///   - `QueryWater` consulte un `WaterSampler` injecté (nullable).
	///   - `SweepCapsule` est un stub MVP qui retourne `hit=false`. La version
	///     complète (heightmap + collision proxies M100.12) viendra avec la
	///     chaîne CHAR-MODEL.
	class WorldColliderImpl : public IWorldCollider
	{
	public:
		/// Branche un sampler. `nullptr` = pas d'eau (QueryWater retourne false).
		void SetWaterSampler(const engine::world::water::WaterSampler* sampler) noexcept;

		bool SweepCapsule(const Capsule& capsule,
			const engine::math::Vec3& startCenter,
			const engine::math::Vec3& endCenter,
			SweepHit& outHit) const override;

		bool QueryWater(const engine::math::Vec3& worldCenter,
			WaterQuery& out) const override;

	private:
		const engine::world::water::WaterSampler* m_waterSampler = nullptr;
	};
}
