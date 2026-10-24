#pragma once

// Monde « Royaumes » — collisionneur du monde en cellules.
//
// Combine :
//   - un collisionneur de BASE (terrain + props chargés en coordonnées de rendu :
//     PNJ, coffres… — typiquement Engine::m_worldCollider) ;
//   - un collisionneur PAR CELLULE résidente (troncs, rochers, murs du décor
//     instancié), stocké en coordonnées LOCALES à sa cellule et décalé par le
//     centre de la cellule exprimé dans le repère de rendu.
// Le décalage étant appliqué à la requête (et non aux primitives), un rebase de
// l'origine ou le déchargement d'une cellule ne coûte rien.
//
// Thread : main thread. Les pointeurs ne sont pas possédés.

#include "src/client/gameplay/CharacterController.h"
#include "src/client/gameplay/CompositeWorldCollider.h"

#include <vector>

namespace engine::gameplay
{
	class CellWorldCollider final : public IWorldCollider
	{
	public:
		/// Collisionneur d'une cellule et son décalage (centre de la cellule en
		/// coordonnées de rendu, m) : position locale = position de rendu − décalage.
		struct CellEntry
		{
			const CompositeWorldCollider* collider = nullptr;
			float offsetX = 0.0f;
			float offsetZ = 0.0f;
		};

		/// \param base collisionneur de base (terrain + props en coordonnées de rendu), peut être nullptr.
		void SetBase(const IWorldCollider* base) { m_base = base; }
		/// Remplace la liste des cellules (appelée à chaque changement de la résidence ou de l'origine).
		void SetCells(std::vector<CellEntry> cells) { m_cells = std::move(cells); }
		std::size_t CellCount() const { return m_cells.size(); }

		/// Hit le plus proche (plus petite fraction) entre la base et les cellules
		/// dont la zone (±5 km + marge autour du décalage) recoupe le balayage.
		bool SweepCapsule(const Capsule& capsule,
			const engine::math::Vec3& startCenter,
			const engine::math::Vec3& endCenter,
			SweepHit& outHit) const override;

		/// Sol seul : délégué à la base (les cellules ne portent que des props).
		bool SweepGround(const Capsule& capsule,
			const engine::math::Vec3& startCenter,
			const engine::math::Vec3& endCenter,
			SweepHit& outHit) const override;

		/// Eau : déléguée à la base (scène d'eau en coordonnées de rendu).
		bool QueryWater(const engine::math::Vec3& worldCenter, WaterQuery& out) const override;

	private:
		const IWorldCollider* m_base = nullptr;
		std::vector<CellEntry> m_cells;
	};
}
