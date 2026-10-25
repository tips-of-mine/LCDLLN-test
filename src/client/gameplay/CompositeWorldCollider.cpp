#include "src/client/gameplay/CompositeWorldCollider.h"

#include <algorithm>
#include <cmath>

namespace engine::gameplay
{
	CompositeWorldCollider::CompositeWorldCollider(const IWorldCollider* terrain)
		: m_terrain(terrain) {}

	void CompositeWorldCollider::Translate(float dx, float dz)
	{
		for (PropCylinder& c : m_cylinders) { c.cx += dx; c.cz += dz; }
		for (PropBox& b : m_boxes) { b.cx += dx; b.cz += dz; }
		m_indexDirty = true;
	}

	bool CompositeWorldCollider::QueryWater(const engine::math::Vec3& worldCenter,
	                                        WaterQuery& out) const
	{
		if (m_terrain) return m_terrain->QueryWater(worldCenter, out);
		out = WaterQuery{};
		return false;
	}

	bool CompositeWorldCollider::SweepGround(const Capsule& capsule,
		const engine::math::Vec3& startCenter, const engine::math::Vec3& endCenter, SweepHit& outHit) const
	{
		if (m_terrain)
			return m_terrain->SweepGround(capsule, startCenter, endCenter, outHit);
		outHit = SweepHit{};
		return false;
	}

	namespace
	{
		/// Clé de case de la grille d'index (x, z entiers signés 32 bits).
		int64_t CellKey(int32_t cx, int32_t cz)
		{
			return (static_cast<int64_t>(cx) << 32) ^ static_cast<int64_t>(static_cast<uint32_t>(cz));
		}
		int32_t CellOf(float v, float cell) { return static_cast<int32_t>(std::floor(v / cell)); }
	}

	void CompositeWorldCollider::RebuildIndex() const
	{
		m_cylCells.clear();
		m_boxCells.clear();
		auto insert = [](std::unordered_map<int64_t, std::vector<uint32_t>>& cells, uint32_t idx,
			float minX, float minZ, float maxX, float maxZ) {
			const int32_t x0 = CellOf(minX, kIndexCellM), x1 = CellOf(maxX, kIndexCellM);
			const int32_t z0 = CellOf(minZ, kIndexCellM), z1 = CellOf(maxZ, kIndexCellM);
			for (int32_t cz = z0; cz <= z1; ++cz)
				for (int32_t cx = x0; cx <= x1; ++cx)
					cells[CellKey(cx, cz)].push_back(idx);
		};
		for (uint32_t i = 0; i < static_cast<uint32_t>(m_cylinders.size()); ++i)
		{
			const PropCylinder& c = m_cylinders[i];
			insert(m_cylCells, i, c.cx - c.radius, c.cz - c.radius, c.cx + c.radius, c.cz + c.radius);
		}
		for (uint32_t i = 0; i < static_cast<uint32_t>(m_boxes.size()); ++i)
		{
			const PropBox& b = m_boxes[i];
			// Demi-étendue XZ du rectangle orienté.
			const float ex = std::fabs(b.axisX.x) * b.halfX + std::fabs(b.axisZ.x) * b.halfZ;
			const float ez = std::fabs(b.axisX.z) * b.halfX + std::fabs(b.axisZ.z) * b.halfZ;
			insert(m_boxCells, i, b.cx - ex, b.cz - ez, b.cx + ex, b.cz + ez);
		}
		m_cylStamp.assign(m_cylinders.size(), 0u);
		m_boxStamp.assign(m_boxes.size(), 0u);
		m_stamp = 0;
		m_indexDirty = false;
	}

	void CompositeWorldCollider::GatherCandidates(float minX, float minZ, float maxX, float maxZ,
		std::vector<uint32_t>& outCyl, std::vector<uint32_t>& outBox) const
	{
		if (m_indexDirty) RebuildIndex();
		outCyl.clear();
		outBox.clear();
		if (++m_stamp == 0u)
		{
			std::fill(m_cylStamp.begin(), m_cylStamp.end(), 0u);
			std::fill(m_boxStamp.begin(), m_boxStamp.end(), 0u);
			m_stamp = 1u;
		}
		const int32_t x0 = CellOf(minX, kIndexCellM), x1 = CellOf(maxX, kIndexCellM);
		const int32_t z0 = CellOf(minZ, kIndexCellM), z1 = CellOf(maxZ, kIndexCellM);
		for (int32_t cz = z0; cz <= z1; ++cz)
			for (int32_t cx = x0; cx <= x1; ++cx)
			{
				const int64_t key = CellKey(cx, cz);
				if (auto it = m_cylCells.find(key); it != m_cylCells.end())
					for (uint32_t i : it->second)
						if (m_cylStamp[i] != m_stamp) { m_cylStamp[i] = m_stamp; outCyl.push_back(i); }
				if (auto it = m_boxCells.find(key); it != m_boxCells.end())
					for (uint32_t i : it->second)
						if (m_boxStamp[i] != m_stamp) { m_boxStamp[i] = m_stamp; outBox.push_back(i); }
			}
		// Ordre croissant = ordre historique du parcours exhaustif (départage des égalités).
		std::sort(outCyl.begin(), outCyl.end());
		std::sort(outBox.begin(), outBox.end());
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
		// Pieds = centre − demi-hauteur, tête = centre + demi-hauteur : MÊME convention
		// que le terrain (TerrainCollider, repos du centre à sol + halfH) et que le
		// rendu de l'avatar (pieds à centre − 0,9 m). L'ancienne convention
		// (± halfH + rayon) plaçait les pieds 0,3 m SOUS le sol pour les props :
		// une dalle de plancher à fleur de terrain devenait une marche, le pas
		// montait le perso, sa tête accrochait le linteau de la porte et il restait
		// coincé en l'air dans l'embrasure.
		const float halfH = capsule.height * 0.5f;
		const float sx = startCenter.x, sz = startCenter.z;
		const float dx = endCenter.x - sx, dz = endCenter.z - sz;

		// Tolérance verticale pour considérer la capsule « posée sur le dessus » d'un
		// prop : tant que ses pieds sont à moins de kStandSkin sous le sommet, on ne
		// bloque plus latéralement (sinon, une fois debout sur le mesh, le test de
		// flanc l'empêcherait d'avancer = perso coincé en haut).
		constexpr float kStandSkin = 0.05f;

		// Candidats de l'index spatial : AABB XZ du balayage élargi du rayon capsule.
		{
			const float r = capsule.radius + 0.05f;
			GatherCandidates(std::min(sx, endCenter.x) - r, std::min(sz, endCenter.z) - r,
				std::max(sx, endCenter.x) + r, std::max(sz, endCenter.z) + r, m_candCyl, m_candBox);
		}

		for (const uint32_t cylIndex : m_candCyl)
		{
			const PropCylinder& c = m_cylinders[cylIndex];
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
				const float bottomStart = startCenter.y - halfH;
				const float bottomEnd   = endCenter.y   - halfH;
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
			const float capLo = endCenter.y - halfH;
			const float capHi = endCenter.y + halfH;
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
		for (const uint32_t boxIndex : m_candBox)
		{
			const PropBox& b = m_boxes[boxIndex];
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
				const float bottomStart = startCenter.y - halfH;
				const float bottomEnd   = endCenter.y   - halfH;
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
			if (b.walkableTop && startCenter.y - halfH >= b.hiY - kStandSkin)
				continue;

			// Recouvrement vertical (identique aux cylindres).
			const float capLo = endCenter.y - halfH;
			const float capHi = endCenter.y + halfH;
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
			// Déjà en chevauchement au départ : comme pour les cylindres, on ne
			// bloque que si le déplacement S'ENFONCE (vers l'intérieur par la face de
			// sortie la plus proche). Sinon le perso peut ressortir ou glisser : sans
			// cette règle, une capsule entrée dans une boîte (pas, téléportation,
			// rebase) y restait figée — seule la rotation répondait.
			const bool startInside = std::fabs(u0) < ex && std::fabs(v0) < ez;
			if (startInside)
			{
				const float penU = ex - std::fabs(u0), penV = ez - std::fabs(v0);
				const bool exitU = penU <= penV;
				const float outward = exitU ? (u0 >= 0.0f ? du : -du) : (v0 >= 0.0f ? dv : -dv);
				if (outward >= 0.0f) continue;
				enterAxis = exitU ? 0 : 1;
				enterSign = exitU ? (u0 >= 0.0f ? 1.0f : -1.0f) : (v0 >= 0.0f ? 1.0f : -1.0f);
			}
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
