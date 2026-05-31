#include "src/client/gameplay/CompositeWorldCollider.h"

#include <cmath>

namespace engine::gameplay
{
	CompositeWorldCollider::CompositeWorldCollider(const IWorldCollider* terrain)
		: m_terrain(terrain) {}

	bool CompositeWorldCollider::QueryWater(const engine::math::Vec3& worldCenter,
	                                        WaterQuery& out) const
	{
		if (m_terrain) return m_terrain->QueryWater(worldCenter, out);
		out = WaterQuery{};
		return false;
	}

	bool CompositeWorldCollider::SweepCapsule(const Capsule& capsule,
		const engine::math::Vec3& startCenter,
		const engine::math::Vec3& endCenter,
		SweepHit& outHit) const
	{
		SweepHit best;
		best.hit = false;
		best.fraction = 1.0f;

		// 1) Terrain (sol). Conserve le hit s'il est plus proche.
		if (m_terrain)
		{
			SweepHit th;
			if (m_terrain->SweepCapsule(capsule, startCenter, endCenter, th) && th.fraction < best.fraction)
				best = th;
		}

		// 2) Cylindres : test 2D cercle (capsule en XZ) contre cercle (cylindre), borné
		//    en Y. On résout le plus petit t in [0,1] où la distance horizontale entre
		//    le centre de la capsule et l'axe du cylindre vaut (rCapsule + rCylindre).
		const float halfH = capsule.height * 0.5f;
		const float sx = startCenter.x, sz = startCenter.z;
		const float dx = endCenter.x - sx, dz = endCenter.z - sz;

		for (const auto& c : m_cylinders)
		{
			const float R = capsule.radius + c.radius;

			// Recouvrement vertical (au point d'arrivée, conservateur) : si la capsule
			// passe entièrement au-dessus ou en dessous du cylindre, pas de collision.
			const float capLo = endCenter.y - halfH - capsule.radius;
			const float capHi = endCenter.y + halfH + capsule.radius;
			if (capHi < c.baseY || capLo > c.topY) continue;

			const float fx = sx - c.cx, fz = sz - c.cz;
			const float a = dx * dx + dz * dz;
			const float b = 2.0f * (fx * dx + fz * dz);
			const float cc = fx * fx + fz * fz - R * R;

			float tHit = -1.0f;
			if (a < 1e-8f)
			{
				// Déplacement XZ négligeable : test statique au point de départ.
				if (cc <= 0.0f) tHit = 0.0f;
			}
			else
			{
				const float disc = b * b - 4.0f * a * cc;
				if (disc >= 0.0f)
				{
					const float sq = std::sqrt(disc);
					const float t0 = (-b - sq) / (2.0f * a);
					const float t1 = (-b + sq) / (2.0f * a);
					if (cc <= 0.0f) tHit = 0.0f;                  // déjà en intersection au départ
					else if (t0 >= 0.0f && t0 <= 1.0f) tHit = t0; // entrée dans le cylindre
					else if (t1 >= 0.0f && t1 <= 1.0f) tHit = t1; // sortie : on bloque quand même
				}
			}

			if (tHit >= 0.0f && tHit < best.fraction)
			{
				best.hit = true;
				best.fraction = tHit;
				// Normale horizontale : de l'axe du cylindre vers la capsule au contact.
				const float px = sx + tHit * dx - c.cx;
				const float pz = sz + tHit * dz - c.cz;
				const float len = std::sqrt(px * px + pz * pz);
				if (len > 1e-6f) best.normal = engine::math::Vec3{ px / len, 0.0f, pz / len };
				else best.normal = engine::math::Vec3{ 1.0f, 0.0f, 0.0f };
			}
		}

		outHit = best;
		return best.hit;
	}
}
