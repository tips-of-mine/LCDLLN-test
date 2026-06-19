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

		// 2) Cylindres de props. Chaque prop est désormais SOLIDE et MARCHABLE :
		//    - capuchon supérieur (2a) : le personnage se pose SUR le mesh (sol,
		//      plateforme…) comme sur le terrain, au lieu de flotter / d'être bloqué ;
		//    - flanc (2b) : barrière horizontale (murs, troncs) tant qu'on est À CÔTÉ.
		const float halfH = capsule.height * 0.5f;
		const float sx = startCenter.x, sz = startCenter.z;
		const float dx = endCenter.x - sx, dz = endCenter.z - sz;

		// Tolérance verticale pour considérer la capsule « posée sur le dessus » d'un
		// prop : tant que ses pieds sont à moins de kStandSkin sous le sommet, on ne
		// bloque plus latéralement (sinon, une fois debout sur le mesh, le test de
		// flanc l'empêcherait d'avancer = perso coincé en haut).
		constexpr float kStandSkin = 0.05f;

		for (const auto& c : m_cylinders)
		{
			// --- 2a) Capuchon supérieur : surface marchable au sommet du prop ---
			// Quand le bas de la capsule descend à travers `topY` au-dessus de
			// l'empreinte XZ du cylindre, on arrête la descente sur le sommet et on
			// renvoie une normale verticale (→ IsWalkable côté CharacterController, le
			// perso se pose dessus). C'est ce qui rend les meshes « marchables ».
			{
				const float bottomStart = startCenter.y - halfH - capsule.radius;
				const float bottomEnd   = endCenter.y   - halfH - capsule.radius;
				const float denom = bottomStart - bottomEnd;   // > 0 si la capsule descend
				if (denom > 1e-6f)
				{
					const float tc = (bottomStart - c.topY) / denom;  // bas de capsule == topY
					if (tc >= 0.0f && tc <= 1.0f && tc < best.fraction)
					{
						const float px = sx + tc * dx - c.cx;
						const float pz = sz + tc * dz - c.cz;
						if (px * px + pz * pz <= c.radius * c.radius)  // au-dessus du disque
						{
							best.hit = true;
							best.fraction = tc;
							best.normal = engine::math::Vec3{ 0.0f, 1.0f, 0.0f };
						}
					}
				}
			}

			// --- 2b) Flanc : barrière horizontale (test 2D cercle vs cercle borné en Y) ---
			const float R = capsule.radius + c.radius;

			// Recouvrement vertical : pas de blocage latéral si la capsule passe
			// entièrement sous la base, OU si ses pieds sont au niveau / au-dessus du
			// sommet (= posée DESSUS — géré par le capuchon ci-dessus, pas à côté).
			const float capLo = endCenter.y - halfH - capsule.radius;
			const float capHi = endCenter.y + halfH + capsule.radius;
			if (capHi < c.baseY || capLo > c.topY - kStandSkin) continue;

			const float fx = sx - c.cx, fz = sz - c.cz;
			const float a = dx * dx + dz * dz;            // mouvement horizontal au carré
			const float cc = fx * fx + fz * fz - R * R;   // < 0 => déjà en chevauchement XZ

			// Le flanc ne concerne QUE le déplacement HORIZONTAL entrant dans le
			// cylindre ; un sweep purement vertical (descente) est traité par le
			// capuchon 2a, pas ici.
			float tHit = -1.0f;
			if (a >= 1e-8f)
			{
				const float b = 2.0f * (fx * dx + fz * dz);  // = 2 d·(start-axe) ; < 0 = on se rapproche
				if (cc <= 0.0f)
				{
					// Déjà en chevauchement : bloquer seulement si on se RAPPROCHE de
					// l'axe (closing). Permet de glisser le long et de ressortir.
					if (b < 0.0f) tHit = 0.0f;
				}
				else
				{
					const float disc = b * b - 4.0f * a * cc;
					if (disc >= 0.0f)
					{
						const float sq = std::sqrt(disc);
						const float t0 = (-b - sq) / (2.0f * a);
						if (t0 >= 0.0f && t0 <= 1.0f) tHit = t0;  // entrée dans le cylindre
					}
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
