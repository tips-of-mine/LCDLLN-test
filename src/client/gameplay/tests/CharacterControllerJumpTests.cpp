// src/client/gameplay/tests/CharacterControllerJumpTests.cpp
//
// Verifie la hauteur du saut (apex) du CharacterController apres le passage a
// un saut realiste (jumpSpeed=4.9 m/s, gravity=-20 m/s^2 -> apex ~0.60 m).
// Garde anti-regression : empeche un retour a un saut irrealiste (~2 m) ou a
// l'absence de saut (apex ~0).
//
// Le test integre le controller a 60 Hz contre un sol plat (FlatFloor) : un
// saut depuis le sol, puis chute jusqu'a retoucher le sol, en suivant l'apex.

#include "src/client/gameplay/CharacterController.h"

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
	using engine::gameplay::IWorldCollider;
	using engine::gameplay::MoveInput;
	using engine::math::Vec3;

	// Sol plat horizontal a y = floorY. Bloque uniquement la composante verticale
	// descendante du sweep (suffisant pour un saut vertical pur). Normale = +Y.
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

			// Mouvement non descendant ou qui ne franchit pas le sol : pas de hit.
			if (endBottom >= m_floorY)
			{
				outHit.hit = false;
				outHit.fraction = 1.0f;
				return false;
			}
			// Deja au sol ou dessous : hit immediat.
			if (startBottom <= m_floorY)
			{
				outHit.hit = true;
				outHit.fraction = 0.0f;
				return true;
			}
			// Franchissement du sol pendant le sweep : fraction = distance avant contact.
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

	// Simule un saut vertical pur et renvoie la hauteur d'apex (m) au-dessus de
	// la position de depart. Le controller part pose au sol (feet a y=0).
	float SimulateJumpApex(float jumpSpeed)
	{
		CharacterController::Config cfg{};
		cfg.gravity = -20.0f;
		cfg.jumpSpeed = jumpSpeed;
		cfg.capsule.height = 1.8f;
		cfg.capsule.radius = 0.3f;
		cfg.enableFlying = false;

		CharacterController cc(cfg);
		// Centre de capsule pose : feet (center.y - height/2) = 0 -> center.y = 0.9.
		const float startCenterY = cfg.capsule.height * 0.5f;
		cc.Init(Vec3(0.0f, startCenterY, 0.0f));

		FlatFloor floor(0.0f);
		const float dt = 1.0f / 60.0f;

		// Quelques frames sans input pour stabiliser l'etat "grounded".
		MoveInput idle{};
		for (int i = 0; i < 10; ++i)
			cc.Update(dt, idle, floor);
		REQUIRE(cc.IsGrounded());

		// Frame de saut.
		MoveInput jump{};
		jump.jumpPressed = true;
		cc.Update(dt, jump, floor);

		// Suit l'apex pendant la phase aerienne (max ~2 s couvre largement).
		float maxCenterY = cc.GetPosition().y;
		for (int i = 0; i < 240; ++i)
		{
			cc.Update(dt, idle, floor);
			const float y = cc.GetPosition().y;
			if (y > maxCenterY) maxCenterY = y;
			if (i > 5 && cc.IsGrounded()) break; // retombe au sol
		}

		return maxCenterY - startCenterY;
	}

	void Test_Jump_Apex_IsRealistic()
	{
		const float apex = SimulateJumpApex(4.9f);
		std::fprintf(stderr, "[INFO] apex(jumpSpeed=4.9) = %.3f m (attendu ~0.60)\n", apex);
		// Theorique 4.9^2 / (2*20) = 0.600 m. Tolerance large pour l'integration
		// discrete a 60 Hz (l'apex Euler peut s'ecarter de quelques cm).
		REQUIRE(apex > 0.50f);
		REQUIRE(apex < 0.72f);
	}

	void Test_Jump_NotTheOldUnrealisticHeight()
	{
		// Garde explicite : l'ancien reglage (9.0) donnait ~2.0 m. On verifie que
		// le saut realiste reste tres en-dessous de la moitie de cette hauteur.
		const float apex = SimulateJumpApex(4.9f);
		REQUIRE(apex < 1.0f);
	}

	void Test_Jump_HigherSpeedGivesHigherApex()
	{
		// Monotonie : plus de jumpSpeed -> apex plus haut (sanity de la simulation).
		const float low = SimulateJumpApex(4.9f);
		const float high = SimulateJumpApex(7.0f);
		REQUIRE(high > low);
	}
}

int main()
{
	Test_Jump_Apex_IsRealistic();
	Test_Jump_NotTheOldUnrealisticHeight();
	Test_Jump_HigherSpeedGivesHigherApex();
	return g_failed;
}
