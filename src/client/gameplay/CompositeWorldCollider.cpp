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
			// Atterrissage sur le DESSUS du prop (couvercle). Le perso peut se poser et
			// se tenir sur tout prop solide. On ne traite QUE les sweeps descendants de
			// PROXIMITE : le bas de la capsule (center.y - halfH, meme convention que le
			// sol/terrain) franchit `topY` de haut en bas, a l'interieur de l'empreinte XZ.
			// GARDE ANTI-SONDE : on ignore les sweeps qui partent loin au-dessus du sommet
			// (ex. sonde anti-encastrement du CharacterController, depuis 50 m), pour
			// preserver "jamais de blocage vertical par un prop" hors atterrissage proche.
			{
				constexpr float kPropTopMargin = 4.0f; // >> demi-capsule, << 50 m (sonde)
				const bool descending = endCenter.y < startCenter.y;
				if (descending && (startCenter.y - c.topY) <= kPropTopMargin)
				{
					const float startBottom = startCenter.y - halfH;
					const float endBottom   = endCenter.y   - halfH;
					const float ex = endCenter.x - c.cx, ez = endCenter.z - c.cz;
					// Zone d'atterrissage alignee sur l'empreinte de blocage horizontal
					// (rayon cylindre + rayon capsule) : "si ca te bloque lateralement,
					// tu peux te poser dessus". Sinon le dessus (petit disque de rayon
					// c.radius) serait quasi impossible a viser en sautant (le blocage
					// lateral maintient le joueur a c.radius+capsule.radius du centre).
					const float landR = c.radius + capsule.radius;
					const bool insideXZ = (ex * ex + ez * ez) <= (landR * landR);
					// Franchissement de topY de haut en bas dans l'empreinte (miroir du
					// sol plat a y=topY). startBottom <= topY => deja pose (frac 0).
					if (insideXZ && endBottom < c.topY && startBottom >= c.topY)
					{
						const float denom = startBottom - endBottom;
						float frac = (denom > 1e-8f) ? (startBottom - c.topY) / denom : 0.0f;
						if (frac < 0.0f) frac = 0.0f;
						if (frac > 1.0f) frac = 1.0f;
						if (frac < best.fraction)
						{
							best.hit = true;
							best.fraction = frac;
							best.normal = engine::math::Vec3{ 0.0f, 1.0f, 0.0f };
						}
						// On est au-dessus : ce cylindre ne bloque pas horizontalement
						// cette frame. Passer au cylindre suivant.
						continue;
					}
				}
			}

			const float R = capsule.radius + c.radius;

			// Recouvrement vertical (au point d'arrivée, conservateur) : si la capsule
			// passe entièrement au-dessus ou en dessous du cylindre, pas de collision.
			const float capLo = endCenter.y - halfH - capsule.radius;
			const float capHi = endCenter.y + halfH + capsule.radius;
			if (capHi < c.baseY || capLo > c.topY) continue;

			const float fx = sx - c.cx, fz = sz - c.cz;
			const float a = dx * dx + dz * dz;            // mouvement horizontal au carré
			const float cc = fx * fx + fz * fz - R * R;   // < 0 => déjà en chevauchement XZ

			// IMPORTANT : la collision contre un prop ne concerne QUE le déplacement
			// HORIZONTAL entrant dans le cylindre. On ne bloque JAMAIS un sweep vertical
			// (gravité, sonde de sol, récupération anti-encastrement du CharacterController
			// qui sonde depuis 50 m au-dessus). Sinon ces sweeps verticaux heurtent le
			// cylindre et la récupération téléporte le perso au sommet de la sonde.
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
