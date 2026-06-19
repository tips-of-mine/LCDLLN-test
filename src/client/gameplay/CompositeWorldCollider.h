#pragma once

#include "src/client/gameplay/CharacterController.h"  // IWorldCollider
#include "src/shared/math/Math.h"

#include <vector>

namespace engine::gameplay
{
	/// Cylindre vertical de collision pour un prop (arbre, coffre, PNJ...).
	/// Axe vertical en (cx, cz), borné en Y par [baseY, topY].
	struct PropCylinder
	{
		float cx = 0.0f;     ///< centre X (monde, mètres)
		float cz = 0.0f;     ///< centre Z (monde, mètres)
		float radius = 0.5f; ///< rayon (mètres)
		float baseY = 0.0f;  ///< Y monde du bas du cylindre
		float topY = 2.0f;   ///< Y monde du haut du cylindre
		/// Porte (mesh « door ») : passage franchissable. Le cylindre n'oppose
		/// AUCUNE collision (ni flanc ni capuchon) → le perso traverse l'embrasure.
		bool passable = false;
		/// Escalier (mesh « escalier ») : surface gravissable. Le flanc ne bloque
		/// pas ; on autorise la montée/descente sur son dessus quelle que soit la
		/// hauteur (le perso s'y pose et la gravit), contrairement à un mur vertical.
		bool stair = false;
	};

	/// Collisionneur composite : combine un IWorldCollider de terrain (sol + eau) et
	/// une liste de cylindres de props. SweepCapsule retourne le hit le plus proche
	/// (plus petite fraction) entre le terrain et les cylindres. QueryWater est délégué
	/// au terrain (la nage reste pilotée par la nappe d'eau, pas par les props).
	///
	/// Le pointeur terrain n'est pas possédé : il doit survivre à ce collisionneur.
	class CompositeWorldCollider final : public IWorldCollider
	{
	public:
		explicit CompositeWorldCollider(const IWorldCollider* terrain = nullptr);

		void SetTerrain(const IWorldCollider* terrain) { m_terrain = terrain; }
		void ClearCylinders() { m_cylinders.clear(); }
		void AddCylinder(const PropCylinder& c) { m_cylinders.push_back(c); }
		std::size_t CylinderCount() const { return m_cylinders.size(); }

		bool SweepCapsule(const Capsule& capsule,
			const engine::math::Vec3& startCenter,
			const engine::math::Vec3& endCenter,
			SweepHit& outHit) const override;

		bool QueryWater(const engine::math::Vec3& worldCenter, WaterQuery& out) const override;

	private:
		const IWorldCollider* m_terrain = nullptr;
		std::vector<PropCylinder> m_cylinders;
	};
}
