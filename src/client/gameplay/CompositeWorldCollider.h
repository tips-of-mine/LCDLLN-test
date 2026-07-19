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
		/// Pièce de bâtiment (mur, paroi) : BARRIÈRE LATÉRALE PURE, SANS dessus
		/// marchable. Les bâtiments (#908) sont approximés par un gros cylindre
		/// englobant PAR pièce ; rendre leur dessus marchable faisait remonter le
		/// perso au SOMMET du mur (la sonde anti-encastrement du contrôleur balaie
		/// depuis le haut et accrochait ce capuchon) = bug « vol contre le mur ».
		/// wall=true désactive le capuchon (2a) : le mur ne fait plus que bloquer
		/// horizontalement. (Les props de décor — arbres, coffres — restent
		/// marchables sur le dessus, eux.)
		bool wall = false;
	};

	/// Boîte de collision orientée pour une pièce de bâtiment (mur, jambage, linteau).
	/// Empreinte = rectangle dans le plan XZ orienté par (axisX, axisZ) unitaires
	/// monde, demi-dimensions (halfX, halfZ) ; bornée en Y par [loY, hiY]. Le sweep
	/// capsule fait un test rectangle-orienté-vs-cercle(rayon capsule) dans XZ +
	/// recouvrement vertical, calqué sur PropCylinder. Pas de dessus marchable
	/// (wall=true) : une boîte de mur ne fait que bloquer latéralement (cf. #919).
	struct PropBox
	{
		float cx = 0.0f, cz = 0.0f;          ///< centre XZ monde (m)
		float halfX = 0.5f, halfZ = 0.1f;    ///< demi-dimensions du rectangle (m), > 0
		engine::math::Vec3 axisX{ 1, 0, 0 }; ///< axe « largeur » monde (unitaire, XZ)
		engine::math::Vec3 axisZ{ 0, 0, 1 }; ///< axe « épaisseur » monde (unitaire, XZ)
		float loY = 0.0f, hiY = 2.0f;        ///< bornes Y monde [bas, haut]
		bool passable = false; ///< aucune collision (battant de porte)
		bool stair = false;    ///< gravissable (cf. CharacterController)
		bool wall = true;      ///< barrière latérale pure, pas de dessus marchable
		/// Roadmap-5 (2026-07-19) — dessus MARCHABLE (capuchon supérieur,
		/// cf. bloc 3a du sweep) : sols d'étage et marches d'escalier. Ne
		/// JAMAIS l'activer sur un mur (wall) — la sonde anti-encastrement
		/// accrocherait son sommet (bug « vol contre le mur », #919/#920).
		bool walkableTop = false;
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
		void AddBox(const PropBox& b) { m_boxes.push_back(b); }
		void ClearBoxes() { m_boxes.clear(); }
		std::size_t BoxCount() const { return m_boxes.size(); }

		bool SweepCapsule(const Capsule& capsule,
			const engine::math::Vec3& startCenter,
			const engine::math::Vec3& endCenter,
			SweepHit& outHit) const override;

		bool QueryWater(const engine::math::Vec3& worldCenter, WaterQuery& out) const override;

	private:
		const IWorldCollider* m_terrain = nullptr;
		std::vector<PropCylinder> m_cylinders;
		std::vector<PropBox> m_boxes;
	};
}
