// src/client/gameplay/tests/CharacterControllerWaterHysteresisTests.cpp
#include "src/client/gameplay/CharacterController.h"

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

	using engine::math::Vec3;
	using engine::gameplay::CharacterController;
	using engine::gameplay::MoveInput;

	// Mini-IWorldCollider qui retourne directement la profondeur configurée
	// (évite la conversion center→feet, donne contrôle parfait sur depth).
	class FakeCollider : public engine::gameplay::IWorldCollider
	{
	public:
		float currentDepth = 0.0f;  // <=0 → out of water

		bool SweepCapsule(const Capsule&, const engine::math::Vec3&,
			const engine::math::Vec3&, SweepHit& outHit) const noexcept override
		{
			outHit = SweepHit{};
			return false;
		}

		bool QueryWater(const engine::math::Vec3&, WaterQuery& out) const noexcept override
		{
			if (currentDepth > 0.0f)
			{
				out.inWater = true;
				out.surfaceY = 10.0f;
				out.depth = currentDepth;
				return true;
			}
			out = WaterQuery{};
			return false;
		}
	};

	// Helper : avancer une frame avec input neutre.
	void StepFrame(CharacterController& cc, const FakeCollider& world)
	{
		MoveInput in{};
		cc.Update(1.0f / 60.0f, in, world);
	}

	void Test_EntersWaterAt1m()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		world.currentDepth = 0.5f;  // shallow → ne déclenche pas Water
		StepFrame(cc, world);

		// La condition d'entrée est depth >= 1.0 — par callback.
		bool entered = false;
		cc.SetWaterTransitionCallbacks(
			[&entered]() { entered = true; },
			nullptr);

		world.currentDepth = 1.1f;
		StepFrame(cc, world);
		REQUIRE(entered);
	}

	void Test_ExitsWaterAt0p7m()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		bool entered = false, exited = false;
		cc.SetWaterTransitionCallbacks(
			[&entered]() { entered = true; },
			[&exited]()  { exited  = true; });

		world.currentDepth = 1.2f;
		StepFrame(cc, world);
		REQUIRE(entered);

		// Profondeur descend à 0.8 → reste en Water (au-dessus du seuil de
		// sortie 0.7).
		world.currentDepth = 0.8f;
		StepFrame(cc, world);
		REQUIRE(!exited);

		// Profondeur descend à 0.6 → sort.
		world.currentDepth = 0.6f;
		StepFrame(cc, world);
		REQUIRE(exited);
	}

	void Test_HysteresisDoesNotFlicker()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		int enterCount = 0, exitCount = 0;
		cc.SetWaterTransitionCallbacks(
			[&enterCount]() { ++enterCount; },
			[&exitCount]()  { ++exitCount;  });

		// Démarre hors eau.
		world.currentDepth = 0.0f;
		StepFrame(cc, world);

		// Oscille 10 frames entre 0.95 et 1.05 (autour du seuil 1.0).
		for (int i = 0; i < 10; ++i)
		{
			world.currentDepth = (i % 2 == 0) ? 1.05f : 0.95f;
			StepFrame(cc, world);
		}
		// Une seule entrée doit être déclenchée (au premier passage >= 1.0),
		// puis 0.95 (depth >= 0.7) reste en Water, 1.05 reste en Water.
		REQUIRE(enterCount == 1);
		REQUIRE(exitCount == 0);
	}

	void Test_OnEnterCallbackFiresOnce()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		int enterCount = 0;
		cc.SetWaterTransitionCallbacks([&enterCount]() { ++enterCount; }, nullptr);

		world.currentDepth = 1.2f;
		StepFrame(cc, world);
		StepFrame(cc, world);  // 2e frame en Water → pas de 2e enter.
		StepFrame(cc, world);

		REQUIRE(enterCount == 1);
	}

	void Test_OnExitCallbackFiresOnce()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;

		int exitCount = 0;
		cc.SetWaterTransitionCallbacks(nullptr, [&exitCount]() { ++exitCount; });

		world.currentDepth = 1.2f;
		StepFrame(cc, world);  // entrée

		world.currentDepth = 0.0f;
		StepFrame(cc, world);  // sortie
		StepFrame(cc, world);  // 2e frame hors eau → pas de 2e exit.

		REQUIRE(exitCount == 1);
	}

	void Test_NoCallbacksSetIsNoCrash()
	{
		CharacterController cc;
		cc.Init(Vec3{ 0, 5, 0 });
		FakeCollider world;
		// Pas de SetWaterTransitionCallbacks() → callbacks restent vides.

		world.currentDepth = 1.2f;
		StepFrame(cc, world);  // doit transitionner sans crash
		world.currentDepth = 0.0f;
		StepFrame(cc, world);  // idem

		// Si on est arrivé ici sans crash, c'est passé.
		REQUIRE(true);
	}
}

int main()
{
	Test_EntersWaterAt1m();
	Test_ExitsWaterAt0p7m();
	Test_HysteresisDoesNotFlicker();
	Test_OnEnterCallbackFiresOnce();
	Test_OnExitCallbackFiresOnce();
	Test_NoCallbacksSetIsNoCrash();
	if (g_failed == 0) std::printf("[OK] 6 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
