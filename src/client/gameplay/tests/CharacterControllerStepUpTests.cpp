// src/client/gameplay/tests/CharacterControllerStepUpTests.cpp
//
// Garde anti-régression du fix « anti-vol » (Lot 5) sur le step-up :
//   un MUR / prop normal (PropCylinder sans flag) ne doit PAS être escaladé.
//   Le step-up est plafonné à maxStep (~0,3 m), donc en marchant droit dans une
//   paroi verticale le perso reste au sol au lieu de « voler » le long du mur
//   (bug observé en jeu : perso flottant collé au mur de l'auberge).
//
// Avant le fix, TryStepUp utilisait maxClimb (3 m) pour TOUT obstacle → toute
// paroi devenait escaladable. On vérifie ici que le perso AVANCE bien jusqu'au
// mur (preuve qu'il marche) MAIS ne monte pas (les pieds restent au sol).
//
// NB : la montée d'ESCALIER fiable n'est PAS couverte ici. Avec un escalier
// mono-cylindre, le contact latéral se fait à (capRadius+cylRadius) de l'axe
// alors que se poser sur le dessus exige d'être à moins de cylRadius : l'écart
// incompressible (= capRadius) empêche un step-up mono-frame fiable. C'est un
// chantier de modèle de collision séparé (marches discrètes ou rampe inclinée).
// Le tag PropCylinder::stair existe (propagation vérifiée par
// CompositeWorldColliderTests) mais ne garantit pas encore une montée lisse.

#include "src/client/gameplay/CharacterController.h"
#include "src/client/gameplay/CompositeWorldCollider.h"

#include <cmath>
#include <cstdio>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::gameplay::CharacterController;
	using engine::gameplay::CompositeWorldCollider;
	using engine::gameplay::IWorldCollider;
	using engine::gameplay::MoveInput;
	using engine::gameplay::PropCylinder;
	using engine::math::Vec3;

	// Sol plat horizontal à y = floorY (normale +Y). Ne bloque que la descente
	// franchissant le sol (suffisant : ici le perso reste posé / glisse).
	class FlatFloor final : public IWorldCollider
	{
	public:
		explicit FlatFloor(float floorY) : m_floorY(floorY) {}

		bool SweepCapsule(const Capsule& capsule,
			const Vec3& startCenter,
			const Vec3& endCenter,
			SweepHit& outHit) const override
		{
			const float halfH = capsule.height * 0.5f;
			const float startBottom = startCenter.y - halfH;
			const float endBottom = endCenter.y - halfH;
			outHit.normal = Vec3(0.0f, 1.0f, 0.0f);
			if (endBottom >= m_floorY) { outHit.hit = false; outHit.fraction = 1.0f; return false; }
			if (startBottom <= m_floorY) { outHit.hit = true; outHit.fraction = 0.0f; return true; }
			const float denom = (startBottom - endBottom);
			float frac = (denom > 0.0f) ? (startBottom - m_floorY) / denom : 0.0f;
			if (frac < 0.0f) frac = 0.0f;
			if (frac > 1.0f) frac = 1.0f;
			outHit.hit = true;
			outHit.fraction = frac;
			return true;
		}

	private:
		float m_floorY = 0.0f;
	};

	// Marche droit vers +x dans un cylindre obstacle (centre x=2, rayon 0,5, de
	// y=0 à y=2) pendant ~2 s et renvoie la position finale du centre de capsule.
	Vec3 SimulatePushIntoWall()
	{
		CharacterController::Config cfg{};
		cfg.gravity = -20.0f;
		cfg.capsule.height = 1.8f;
		cfg.capsule.radius = 0.3f;
		cfg.enableFlying = false;
		cfg.maxStep = 0.3f;
		cfg.maxClimb = 3.0f;
		cfg.maxSlopeDeg = 45.0f;

		CharacterController cc(cfg);
		const float halfH = cfg.capsule.height * 0.5f;
		cc.Init(Vec3(0.0f, halfH, 0.0f)); // pieds à y=0

		FlatFloor floor(0.0f);
		CompositeWorldCollider world(&floor);
		PropCylinder wall{ 2.0f, 0.0f, 0.5f, 0.0f, 2.0f }; // mur de 2 m, non taggé
		world.AddCylinder(wall);

		const float dt = 1.0f / 60.0f;
		MoveInput idle{};
		for (int i = 0; i < 10; ++i) // stabilise l'état "grounded" avant de marcher
			cc.Update(dt, idle, world);

		MoveInput in{};
		in.moveDirXZ = Vec3(1.0f, 0.0f, 0.0f); // marche vers le mur
		for (int i = 0; i < 120; ++i)
			cc.Update(dt, in, world);

		return cc.GetPosition();
	}

	void Test_Wall_NotClimbed_ButReached()
	{
		const Vec3 pos = SimulatePushIntoWall();
		const float feetY = pos.y - 0.9f; // halfH = 0.9
		std::fprintf(stderr, "[INFO] mur: x=%.3f feetY=%.3f (attendu x>0.8 atteint, feetY<0.5 pas de vol)\n",
			pos.x, feetY);
		// Le perso a bien AVANCÉ vers le mur (contact ~x=1.2 = axe2 - (cap0.3+cyl0.5)).
		REQUIRE(pos.x > 0.8f);
		// ...et n'a PAS été hissé le long de la paroi (anti-vol) : pieds au sol.
		REQUIRE(feetY < 0.5f);
	}

	// Garde du fix « vol contre le mur de bâtiment » : un perso DANS l'empreinte
	// d'un gros cylindre de mur (wall=true, comme l'auberge) ne doit PAS être
	// remonté à son sommet par la sonde anti-encastrement du contrôleur. Sans le
	// flag wall (capuchon marchable), la sonde — qui balaie du haut vers le bas —
	// accrochait le sommet (topY) et collait le perso à la hauteur du mur.
	void Test_BuildingWall_NoFloatSnap()
	{
		CharacterController::Config cfg{};
		cfg.gravity = -20.0f;
		cfg.capsule.height = 1.8f;
		cfg.capsule.radius = 0.3f;
		cfg.enableFlying = false;
		cfg.maxStep = 0.3f;
		cfg.maxClimb = 3.0f;
		cfg.maxSlopeDeg = 45.0f;

		CharacterController cc(cfg);
		const float halfH = cfg.capsule.height * 0.5f;
		cc.Init(Vec3(0.0f, halfH, 0.0f)); // pieds à y=0, DANS l'empreinte du mur

		FlatFloor floor(0.0f);
		CompositeWorldCollider world(&floor);
		// Gros cylindre de mur de 3 m, rayon 3 m (englobant grossier d'une pièce
		// de bâtiment), centré sur le perso. wall=true => pas de dessus marchable.
		PropCylinder wall{ 0.0f, 0.0f, 3.0f, 0.0f, 3.0f };
		wall.wall = true;
		world.AddCylinder(wall);

		const float dt = 1.0f / 60.0f;
		MoveInput idle{};
		for (int i = 0; i < 30; ++i)
			cc.Update(dt, idle, world);

		const float feetY = cc.GetPosition().y - halfH;
		std::fprintf(stderr, "[INFO] mur batiment: feetY=%.3f (attendu ~0, PAS remonte a topY=3)\n", feetY);
		// Reste au sol : la sonde ne l'a pas collé au sommet du mur (sinon feetY ~3).
		REQUIRE(feetY < 0.5f);
	}
}

int main()
{
	Test_Wall_NotClimbed_ButReached();
	Test_BuildingWall_NoFloatSnap();
	if (g_failed == 0) std::fprintf(stderr, "CharacterControllerStepUpTests: OK\n");
	return g_failed;
}
