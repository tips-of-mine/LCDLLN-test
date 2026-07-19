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
			// Porte (mesh « door ») : passage franchissable → le cylindre n'oppose
			// AUCUNE collision (ni capuchon ni flanc), le perso traverse l'embrasure.
			if (c.passable) continue;

			// --- 2a) Capuchon supérieur : surface marchable au sommet du prop ---
			// Quand le bas de la capsule descend à travers `topY` au-dessus de
			// l'empreinte XZ du cylindre, on arrête la descente sur le sommet et on
			// renvoie une normale verticale (→ IsWalkable côté CharacterController, le
			// perso se pose dessus). C'est ce qui rend les meshes « marchables ».
			//
			// EXCEPTION mur (c.wall) : pas de dessus marchable. Un mur de bâtiment est
			// un gros cylindre englobant ; son capuchon faisait accrocher la sonde
			// anti-encastrement du contrôleur (balayage du haut vers le bas) et
			// remontait le perso au sommet du mur = « vol contre le mur ». On saute
			// donc 2a pour les murs : ils ne font que bloquer latéralement (2b).
			if (!c.wall)
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
							best.stair = c.stair;
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
				best.stair = c.stair;
				// Normale horizontale : de l'axe du cylindre vers la capsule au contact.
				const float px = sx + tHit * dx - c.cx;
				const float pz = sz + tHit * dz - c.cz;
				const float len = std::sqrt(px * px + pz * pz);
				if (len > 1e-6f) best.normal = engine::math::Vec3{ px / len, 0.0f, pz / len };
				else best.normal = engine::math::Vec3{ 1.0f, 0.0f, 0.0f };
			}
		}

		// 3) Boîtes orientées de props (murs/jambages/linteaux de bâtiment).
		//    Même découpage que les cylindres : recouvrement Y + empreinte XZ, mais
		//    l'empreinte est un rectangle orienté au lieu d'un disque.
		for (const auto& b : m_boxes)
		{
			if (b.passable) continue;

			// --- 3a) Capuchon supérieur (Roadmap-5, 2026-07-19) : dessus
			// MARCHABLE (sols d'étage, marches d'escalier). Même mécanique que
			// le capuchon cylindre 2a : quand le bas de la capsule descend à
			// travers hiY au-dessus de l'empreinte du rectangle, on pose le
			// perso (normale verticale → IsWalkable). JAMAIS actif sur les
			// murs (walkableTop=false) — la sonde anti-encastrement du
			// contrôleur accrocherait leur sommet (« vol contre le mur »).
			if (b.walkableTop)
			{
				const float bottomStart = startCenter.y - halfH - capsule.radius;
				const float bottomEnd   = endCenter.y   - halfH - capsule.radius;
				const float denom = bottomStart - bottomEnd; // > 0 si la capsule descend
				if (denom > 1e-6f)
				{
					const float tc = (bottomStart - b.hiY) / denom; // bas de capsule == hiY
					if (tc >= 0.0f && tc <= 1.0f && tc < best.fraction)
					{
						// Point d'impact XZ projeté dans le repère du rectangle
						// (empreinte élargie du rayon de capsule, comme le flanc).
						const float pxw = sx + tc * dx - b.cx;
						const float pzw = sz + tc * dz - b.cz;
						const float pu = pxw * b.axisX.x + pzw * b.axisX.z;
						const float pv = pxw * b.axisZ.x + pzw * b.axisZ.z;
						if (std::fabs(pu) <= b.halfX + capsule.radius
							&& std::fabs(pv) <= b.halfZ + capsule.radius)
						{
							best.hit = true;
							best.fraction = tc;
							best.normal = engine::math::Vec3{ 0.0f, 1.0f, 0.0f };
							best.stair = b.stair;
						}
					}
				}
			}

			// Le flanc ne concerne QUE le déplacement HORIZONTAL (même garde
			// que le flanc des cylindres, bloc 2) : un sweep purement vertical
			// (chute/pose) est l'affaire du capuchon 3a — sans cette garde, le
			// slab-test ci-dessous rend tEnter=0 dès que le départ est dans
			// l'empreinte XZ et écrase la pose avec une normale horizontale.
			if (dx * dx + dz * dz < 1e-8f) continue;

			// Dessus marchable : une capsule qui DÉMARRE au-dessus du dessus
			// (à la peau près) ne peut pas heurter le flanc — sinon on serait
			// bloqué latéralement en marchant sur la dalle ou en atterrissant
			// dessus avec un déplacement diagonal.
			if (b.walkableTop && startCenter.y - halfH - capsule.radius >= b.hiY - kStandSkin)
				continue;

			// Recouvrement vertical (identique aux cylindres).
			const float capLo = endCenter.y - halfH - capsule.radius;
			const float capHi = endCenter.y + halfH + capsule.radius;
			if (capHi < b.loY || capLo > b.hiY - kStandSkin) continue;

			// Projection du départ et du déplacement XZ dans le repère du rectangle.
			const float r = capsule.radius;
			const float rx = sx - b.cx, rz = sz - b.cz;
			const float u0 = rx * b.axisX.x + rz * b.axisX.z; // le long de axisX
			const float v0 = rx * b.axisZ.x + rz * b.axisZ.z; // le long de axisZ
			const float du = dx * b.axisX.x + dz * b.axisX.z;
			const float dv = dx * b.axisZ.x + dz * b.axisZ.z;

			// AABB 2D élargi du rayon capsule (Minkowski cercle-vs-rectangle).
			const float ex = b.halfX + r;
			const float ez = b.halfZ + r;

			// Slab test 2D (ray (u0,v0)+t(du,dv) vs [-ex,ex]x[-ez,ez]).
			float tEnter = 0.0f, tExit = 1.0f;
			int enterAxis = -1;     // 0 = axe u (axisX), 1 = axe v (axisZ)
			float enterSign = 0.0f; // signe de la face d'entrée
			bool separated = false;

			// Lambda slab sur un axe : met à jour tEnter/tExit/enterAxis.
			auto slab = [&](float p, float d, float e, int axis) {
				if (std::fabs(d) < 1e-8f) { if (p < -e || p > e) separated = true; return; }
				float t1 = (-e - p) / d, t2 = (e - p) / d;
				float sgn = -1.0f; // face -e si d>0 (on entre par -e)
				if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; sgn = 1.0f; }
				if (t1 > tEnter) { tEnter = t1; enterAxis = axis; enterSign = sgn; }
				if (t2 < tExit) tExit = t2;
			};
			slab(u0, du, ex, 0);
			slab(v0, dv, ez, 1);

			if (separated || tEnter > tExit || tEnter > 1.0f) continue;
			float tHit = tEnter < 0.0f ? 0.0f : tEnter; // déjà à l'intérieur -> bloque à 0
			if (tHit >= best.fraction) continue;

			best.hit = true;
			best.fraction = tHit;
			best.stair = b.stair;
			// Normale = face d'entrée, exprimée en monde (axe XZ correspondant).
			engine::math::Vec3 n{ 1, 0, 0 };
			if (enterAxis == 0) n = engine::math::Vec3{ enterSign * b.axisX.x, 0.0f, enterSign * b.axisX.z };
			else if (enterAxis == 1) n = engine::math::Vec3{ enterSign * b.axisZ.x, 0.0f, enterSign * b.axisZ.z };
			else { // déjà à l'intérieur (tEnter<=0) : normale = sortie la plus proche en u.
				n = engine::math::Vec3{ (u0 >= 0.0f ? b.axisX.x : -b.axisX.x), 0.0f, (u0 >= 0.0f ? b.axisX.z : -b.axisX.z) };
			}
			const float nlen = std::sqrt(n.x * n.x + n.z * n.z);
			best.normal = nlen > 1e-6f ? engine::math::Vec3{ n.x / nlen, 0.0f, n.z / nlen }
			                           : engine::math::Vec3{ 1.0f, 0.0f, 0.0f };
		}

		outHit = best;
		return best.hit;
	}
}
