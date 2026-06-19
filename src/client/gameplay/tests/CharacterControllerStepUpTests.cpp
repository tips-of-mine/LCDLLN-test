// src/client/gameplay/tests/CharacterControllerStepUpTests.cpp
//
// Garde anti-régression du fix « anti-vol » (Lot 5) sur le step-up :
//   - un MUR / prop normal (PropCylinder sans flag) ne doit PAS être escaladé :
//     le step-up est plafonné à maxStep (~0,3 m), donc en se collant à une paroi
//     verticale le perso reste au sol au lieu de « voler » le long du mur ;
//   - un ESCALIER (PropCylinder::stair) reste gravissable : le step-up monte
//     jusqu'à maxClimb, le perso se pose sur le dessus.
//
// Le test intègre le CharacterController à 60 Hz contre un CompositeWorldCollider
// (sol plat + un cylindre obstacle de 2 m) en marchant droit dans l'obstacle.

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
	// franchissant le sol (suffisant : ici le perso reste posé / monte).
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
	// y=0 à y=2) pendant ~2 s et renvoie la hauteur des pieds (center.y - halfH).
	// \param stair  tague l'obstacle comme escalier (gravissable) ou mur (non).
	float SimulatePushIntoCylinder(bool stair)
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
		// Obstacle de 2 m : franchissable par maxClimb=3 (lève le bas de capsule
		// au-dessus de topY=2) mais PAS par maxStep=0,3.
		PropCylinder obstacle{ 2.0f, 0.0f, 0.5f, 0.0f, 2.0f };
		obstacle.stair = stair;
		world.AddCylinder(obstacle);

		const float dt = 1.0f / 60.0f;
		MoveInput in{};
		in.moveDirXZ = Vec3(1.0f, 0.0f, 0.0f); // vers le cylindre
		for (int i = 0; i < 120; ++i)
			cc.Update(dt, in, world);

		return cc.GetPosition().y - halfH;
	}

	void Test_Wall_NotClimbed()
	{
		const float feetY = SimulatePushIntoCylinder(/*stair*/ false);
		std::fprintf(stderr, "[INFO] mur: pieds y=%.3f (attendu ~0, plafonne maxStep)\n", feetY);
		// Anti-vol : le perso reste au sol (jamais hissé au sommet du mur de 2 m).
		REQUIRE(feetY < 0.5f);
	}

	void Test_Stair_Climbed()
	{
		const float feetY = SimulatePushIntoCylinder(/*stair*/ true);
		std::fprintf(stderr, "[INFO] escalier: pieds y=%.3f (attendu > 1.5, gravi)\n", feetY);
		// L'escalier taggé est gravi : le perso se pose sur le dessus (topY=2).
		REQUIRE(feetY > 1.5f);
	}
}

int main()
{
	Test_Wall_NotClimbed();
	Test_Stair_Climbed();
	if (g_failed == 0) std::fprintf(stderr, "CharacterControllerStepUpTests: OK\n");
	return g_failed;
}
