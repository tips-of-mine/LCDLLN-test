// src/client/gameplay/tests/CharacterControllerSwimTests.cpp
//
// Nage en eau profonde (mer, grands lacs du monde « Royaumes ») : sans
// « plonger », le nageur remonte vers la surface et s'y stabilise ; en
// plongeant (swimDownPressed), il descend. Avant la poussée d'Archimède, un
// nageur entré dans 7 m d'eau coulait jusqu'au fond (caméra sous l'eau, mer
// invisible).

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

	/// Mer : fond plat à y = floorY, surface à y = surfaceY (partout).
	class Sea final : public IWorldCollider
	{
	public:
		Sea(float floorY, float surfaceY) : m_floorY(floorY), m_surfaceY(surfaceY) {}

		bool SweepCapsule(const Capsule& capsule, const Vec3& startCenter, const Vec3& endCenter,
			SweepHit& outHit) const override
		{
			const float halfH = capsule.height * 0.5f;
			const float startBottom = startCenter.y - halfH;
			const float endBottom = endCenter.y - halfH;
			outHit.normal = Vec3(0.0f, 1.0f, 0.0f);
			if (endBottom >= m_floorY) { outHit.hit = false; outHit.fraction = 1.0f; return false; }
			if (startBottom <= m_floorY) { outHit.hit = true; outHit.fraction = 0.0f; return true; }
			const float denom = startBottom - endBottom;
			outHit.hit = true;
			outHit.fraction = denom > 0.0f ? (startBottom - m_floorY) / denom : 0.0f;
			return true;
		}

		bool QueryWater(const Vec3& worldCenter, WaterQuery& out) const override
		{
			out.surfaceY = m_surfaceY;
			out.depth = m_surfaceY - worldCenter.y;
			out.inWater = out.depth > 0.0f;
			return out.inWater;
		}

	private:
		float m_floorY;
		float m_surfaceY;
	};

	/// Simule \p seconds à 60 Hz dans 7 m d'eau, départ au fond. \return centre Y final.
	float Simulate(bool dive, float seconds, float startCenterY)
	{
		CharacterController::Config cfg{};
		cfg.gravity = -20.0f;
		cfg.enableFlying = false;
		CharacterController cc(cfg);
		cc.Init(Vec3(0.0f, startCenterY, 0.0f));
		const Sea sea(/*floor*/ 33.0f, /*surface*/ 40.0f);
		MoveInput in{};
		in.swimDownPressed = dive;
		const float dt = 1.0f / 60.0f;
		for (int i = 0; i < static_cast<int>(seconds * 60.0f); ++i)
			cc.Update(dt, in, sea);
		return cc.GetPosition().y;
	}

	void Test_Floats_Up_To_Surface()
	{
		const float y = Simulate(/*dive*/ false, 10.0f, 33.0f + 0.9f);
		std::fprintf(stderr, "[INFO] centre apres 10 s sans plonger : %.2f (surface 40)\n", y);
		// Le centre se stabilise juste sous la surface (bande de remontée de 0,25 m).
		REQUIRE(y > 39.0f);
		REQUIRE(y < 40.6f);
	}

	void Test_Dive_Goes_Down()
	{
		const float y = Simulate(/*dive*/ true, 5.0f, 39.8f);
		std::fprintf(stderr, "[INFO] centre apres 5 s de plongee : %.2f\n", y);
		REQUIRE(y < 37.0f);
	}
}

int main()
{
	Test_Floats_Up_To_Surface();
	Test_Dive_Goes_Down();
	if (g_failed == 0)
		std::printf("character_controller_swim_tests: OK\n");
	return g_failed == 0 ? 0 : 1;
}
