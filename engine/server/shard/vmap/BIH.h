#pragma once
// CMANGOS.05 (Phase 2.05a) — BIH<T> : Bounding Interval Hierarchy
// templated. Structure d'accélération pour le raycast contre une
// collection d'objets bornés (bbox connue à la construction).
//
// **Choix simplificateur** : on stocke les indices d'objets dans un
// tableau plat ; chaque feuille est un range [first, last) dans ce
// tableau. La méthode de split est "median over largest axis" — pas
// optimale (un SAH ferait mieux) mais déterministe et suffisante en
// première mise.
//
// **Construction** : O(N log N) en moyenne. **Raycast** : O(log N) en
// moyenne, O(N) au pire. Pas de threading interne.
//
// Pour LCDLLN, le concept template T est minimal :
//   - T(...).Bounds() → AABB
//   - intersection rayon ↔ T fournie via une lambda passée à `Raycast`.
//
// La BIH ne fait PAS l'intersection finale rayon-vs-objet : elle
// **filtre** les candidats via leurs AABB et délègue le test précis
// au caller. C'est le contrat classique d'une structure d'accélération.

#include "engine/server/shard/vmap/AABB.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace engine::server::shard::vmap
{
	/// Configuration de construction.
	struct BIHBuildConfig
	{
		/// Nombre max d'objets par feuille (au-delà : split). Default 8.
		size_t leafThreshold = 8;
		/// Profondeur max du split (sécurité). Default 32.
		size_t maxDepth = 32;
	};

	template<typename T>
	class BIH
	{
	public:
		BIH() = default;

		/// Construit la BIH à partir d'un set d'objets. La BIH garde une
		/// **référence** aux objets via leurs indices ; les objets eux-mêmes
		/// restent dans le tableau du caller (pas de copie).
		void Build(const std::vector<T>& items, BIHBuildConfig cfg = {});

		/// Vide la structure.
		void Clear() noexcept
		{
			m_nodes.clear();
			m_indices.clear();
			m_globalBox = AABB::Empty();
		}

		/// Bbox englobante de tous les objets (root). Empty si la BIH est vide.
		const AABB& GlobalBox() const noexcept { return m_globalBox; }

		/// Nombre de noeuds (interne + feuilles).
		size_t NodeCount() const noexcept { return m_nodes.size(); }

		/// Nombre d'objets indexés.
		size_t ItemCount() const noexcept { return m_indices.size(); }

		/// Lance un raycast et invoque \p hitTest(itemIndex, ray, &tHit) pour
		/// chaque candidat (objet dont la AABB est traversée par le rayon).
		/// `hitTest` retourne true si l'objet est touché et écrit `tHit`
		/// dans sa sortie. Le BIH s'arrête au premier hit avec t le plus
		/// petit (modulo l'ordre des candidats — pour un "any hit"
		/// strictement le plus proche, le caller peut accumuler).
		///
		/// Retourne le `t` du hit le plus proche, ou +∞ si aucun hit.
		template<typename HitFn>
		float Raycast(const Ray& ray, const std::vector<T>& items, HitFn hitTest) const;

	private:
		/// Noeud BIH : intern (split) ou feuille (range). Encodage compact :
		/// si `leafCount > 0`, c'est une feuille couvrant
		/// `m_indices[leafFirst .. leafFirst+leafCount)`. Sinon, noeud interne
		/// avec `axis ∈ {0,1,2}`, `clipL` et `clipR` les bornes de split sur
		/// l'axe, et `left`/`right` les indices vers les enfants.
		struct Node
		{
			AABB     box;            // bbox du sous-arbre (debug + traversée)
			uint32_t leafFirst = 0;  // si leaf : index dans m_indices
			uint32_t leafCount = 0;  // 0 ⇒ noeud interne, >0 ⇒ feuille
			uint32_t left  = 0;      // si interne : index du child left
			uint32_t right = 0;      // si interne : index du child right
			float    clipL = 0.0f;
			float    clipR = 0.0f;
			uint8_t  axis  = 0;      // 0=x, 1=y, 2=z
		};

		uint32_t BuildRecurse(std::vector<uint32_t>& indices,
			const std::vector<T>& items, size_t first, size_t count,
			const AABB& parentBox, size_t depth, const BIHBuildConfig& cfg);

		std::vector<Node>     m_nodes;
		std::vector<uint32_t> m_indices;
		AABB                  m_globalBox = AABB::Empty();
	};

	// Implémentation inline — la BIH étant un template, elle doit être
	// définie en header.

	template<typename T>
	void BIH<T>::Build(const std::vector<T>& items, BIHBuildConfig cfg)
	{
		Clear();
		if (items.empty())
			return;

		// Construire la liste d'indices initiale + bbox globale.
		std::vector<uint32_t> indices;
		indices.reserve(items.size());
		AABB world = AABB::Empty();
		for (uint32_t i = 0; i < items.size(); ++i)
		{
			indices.push_back(i);
			world.Expand(items[i].Bounds());
		}
		m_globalBox = world;

		// On réserve à l'avance pour limiter les reallocs (estim 2N nodes).
		m_nodes.reserve(2 * items.size() + 8);

		BuildRecurse(indices, items, 0, indices.size(), world, 0, cfg);

		m_indices = std::move(indices);
	}

	template<typename T>
	uint32_t BIH<T>::BuildRecurse(std::vector<uint32_t>& indices,
		const std::vector<T>& items, size_t first, size_t count,
		const AABB& parentBox, size_t depth, const BIHBuildConfig& cfg)
	{
		const uint32_t myIndex = static_cast<uint32_t>(m_nodes.size());
		m_nodes.push_back(Node{});
		Node& self = m_nodes.back();
		self.box = parentBox;

		// Cas feuille : peu d'éléments OU profondeur max.
		if (count <= cfg.leafThreshold || depth >= cfg.maxDepth)
		{
			self.leafFirst = static_cast<uint32_t>(first);
			self.leafCount = static_cast<uint32_t>(count);
			return myIndex;
		}

		// Choix d'axe : le plus long (gros span = meilleur split en moyenne).
		const Vec3 size(
			parentBox.max.x - parentBox.min.x,
			parentBox.max.y - parentBox.min.y,
			parentBox.max.z - parentBox.min.z);
		uint8_t axis = 0;
		if (size.y > size.x) axis = 1;
		if (axis == 0 ? size.z > size.x : size.z > size.y) axis = 2;

		// Split par median des centres sur l'axe.
		const auto centerOnAxis = [&](uint32_t idx) {
			const auto box = items[idx].Bounds();
			switch (axis)
			{
				case 0: return 0.5f * (box.min.x + box.max.x);
				case 1: return 0.5f * (box.min.y + box.max.y);
				case 2: return 0.5f * (box.min.z + box.max.z);
			}
			return 0.0f;
		};

		const size_t mid = first + count / 2;
		std::nth_element(indices.begin() + first,
			indices.begin() + mid,
			indices.begin() + first + count,
			[&](uint32_t a, uint32_t b) { return centerOnAxis(a) < centerOnAxis(b); });

		// clipL = max sur axe des objets de gauche ; clipR = min des objets de droite.
		// Ces deux valeurs définissent les "intervalles" BIH (cf. Wächter & Keller).
		float clipL = -std::numeric_limits<float>::infinity();
		float clipR =  std::numeric_limits<float>::infinity();
		AABB leftBox  = AABB::Empty();
		AABB rightBox = AABB::Empty();
		for (size_t i = first; i < mid; ++i)
		{
			const auto box = items[indices[i]].Bounds();
			leftBox.Expand(box);
			float maxA = (axis == 0) ? box.max.x : (axis == 1) ? box.max.y : box.max.z;
			if (maxA > clipL) clipL = maxA;
		}
		for (size_t i = mid; i < first + count; ++i)
		{
			const auto box = items[indices[i]].Bounds();
			rightBox.Expand(box);
			float minA = (axis == 0) ? box.min.x : (axis == 1) ? box.min.y : box.min.z;
			if (minA < clipR) clipR = minA;
		}

		// Si le split est dégénéré (un côté vide ou 100% chevauchement),
		// fallback en feuille — évite la récursion infinie.
		if (mid == first || mid == first + count)
		{
			Node& selfRef = m_nodes[myIndex];
			selfRef.leafFirst = static_cast<uint32_t>(first);
			selfRef.leafCount = static_cast<uint32_t>(count);
			return myIndex;
		}

		const uint32_t leftIdx  = BuildRecurse(indices, items, first, mid - first, leftBox, depth + 1, cfg);
		const uint32_t rightIdx = BuildRecurse(indices, items, mid, first + count - mid, rightBox, depth + 1, cfg);

		Node& selfRef = m_nodes[myIndex];
		selfRef.axis  = axis;
		selfRef.left  = leftIdx;
		selfRef.right = rightIdx;
		selfRef.clipL = clipL;
		selfRef.clipR = clipR;
		return myIndex;
	}

	template<typename T>
	template<typename HitFn>
	float BIH<T>::Raycast(const Ray& ray, const std::vector<T>& items, HitFn hitTest) const
	{
		float bestT = ray.tMax;
		if (m_nodes.empty())
			return std::numeric_limits<float>::infinity();

		// Stack pile pour traversée itérative (évite la récursion).
		uint32_t stack[64];
		int sp = 0;
		stack[sp++] = 0;

		while (sp > 0)
		{
			const uint32_t ni = stack[--sp];
			const Node& n = m_nodes[ni];

			float tEnter = 0.0f;
			Ray clipped = ray;
			clipped.tMax = bestT;
			if (!IntersectRayAABB(clipped, n.box, tEnter))
				continue;

			if (n.leafCount > 0)
			{
				// Feuille : test exact sur chaque objet.
				for (uint32_t k = 0; k < n.leafCount; ++k)
				{
					const uint32_t itemIdx = m_indices[n.leafFirst + k];
					float tHit = std::numeric_limits<float>::infinity();
					if (hitTest(itemIdx, ray, tHit) && tHit < bestT && tHit >= ray.tMin)
						bestT = tHit;
				}
				continue;
			}

			// Noeud interne : push les deux enfants. (Optimisation possible :
			// near/far order par signe de dir[axis] — TODO si profil le justifie.)
			if (sp + 2 <= 64)
			{
				stack[sp++] = n.left;
				stack[sp++] = n.right;
			}
		}
		return (bestT < ray.tMax) ? bestT : std::numeric_limits<float>::infinity();
	}
}
