// src/client/world/hazard/tests/HazardSimulatorTests.cpp
#include "src/client/world/hazard/HazardSimulator.h"
#include "src/client/world/hazard/HazardVolumes.h"

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

	using engine::math::Vec3;
	using engine::world::hazard::EscapeMode;
	using engine::world::hazard::HazardCallbacks;
	using engine::world::hazard::HazardEffect;
	using engine::world::hazard::HazardInstance;
	using engine::world::hazard::HazardScene;
	using engine::world::hazard::HazardShape;
	using engine::world::hazard::HazardSimulator;
	using engine::world::hazard::HazardType;

	HazardScene MakeQuicksandScene()
	{
		HazardScene s;
		HazardInstance h{ HazardType::Quicksand, HazardShape::Cylinder,
			Vec3{0, 0, 0}, Vec3{2, 1, 2}, 5.0f, 2.0f,
			0.15f, 1.8f, 0.10f, EscapeMode::MashButton, 0 };
		s.hazards.push_back(h);
		return s;
	}

	HazardScene MakeBogScene()
	{
		HazardScene s;
		HazardInstance h{ HazardType::Bog, HazardShape::Cylinder,
			Vec3{0, 0, 0}, Vec3{2, 1, 2}, 5.0f, 2.0f,
			0.08f, 100.0f, 0.20f, EscapeMode::LateralMove, 0 };
		s.hazards.push_back(h);
		return s;
	}

	HazardScene MakeLavaScene()
	{
		HazardScene s;
		HazardInstance h{ HazardType::LavaSurface, HazardShape::Cylinder,
			Vec3{0, 0, 0}, Vec3{2, 1, 2}, 5.0f, 2.0f,
			0.0f, 0.0f, 0.0f, EscapeMode::None, 0 };
		s.hazards.push_back(h);
		return s;
	}

	void Test_HazardSimulator_SinkRate()
	{
		HazardScene scene = MakeQuicksandScene();
		HazardSimulator sim;
		sim.Init(scene, HazardCallbacks{});

		// Tick 1 s à 0.1 s par frame, player au centre du cylindre.
		for (int i = 0; i < 10; ++i)
		{
			sim.Update(0.1f, Vec3{0, 0.5f, 0}, false);
		}
		// Après 10 frames × 0.1 s × 0.15 m/s = 0.15 m.
		REQUIRE(std::fabs(sim.State().currentDepth - 0.15f) < 1e-4f);
	}

	void Test_HazardSimulator_MashEscape()
	{
		HazardScene scene = MakeQuicksandScene();
		HazardSimulator sim;
		bool exited = false;
		HazardCallbacks cb;
		cb.onExit = [&exited]() { exited = true; };
		sim.Init(scene, cb);

		// 10 appuis sur 10 frames de 0.1 s.
		for (int i = 0; i < 10; ++i)
		{
			sim.Update(0.1f, Vec3{0, 0.5f, 0}, true);
		}
		REQUIRE(exited);
	}

	void Test_HazardSimulator_LateralEscape()
	{
		HazardScene scene = MakeBogScene();
		HazardSimulator sim;
		bool exited = false;
		HazardCallbacks cb;
		cb.onExit = [&exited]() { exited = true; };
		sim.Init(scene, cb);

		// Démarre au centre, fait des steps de 0.3 m latéraux.
		// Premier tick : pas de déplacement (m_hasLastPos=false → 0).
		// Frames suivantes : +0.3 m latéral chacune.
		Vec3 pos{0, 0.5f, 0};
		sim.Update(0.1f, pos, false);  // entrée

		// Fait 8 steps de 0.3 m → 2.4 m cumulés, dépasse seuil 2.0 m.
		for (int i = 0; i < 8; ++i)
		{
			pos.x += 0.3f;
			sim.Update(0.1f, pos, false);
			if (exited) break;
		}
		REQUIRE(exited);
	}

	void Test_HazardSimulator_LavaKills3s()
	{
		HazardScene scene = MakeLavaScene();
		HazardSimulator sim;
		int dieCount = 0;
		std::string lastReason;
		HazardCallbacks cb;
		cb.die = [&dieCount, &lastReason](std::string_view reason) {
			++dieCount;
			lastReason = std::string(reason);
		};
		sim.Init(scene, cb);

		// 30 frames de 0.1 s = 3.0 s exactement.
		for (int i = 0; i < 30; ++i)
		{
			sim.Update(0.1f, Vec3{0, 0.5f, 0}, false);
			if (dieCount > 0) break;
		}
		REQUIRE(dieCount == 1);
		REQUIRE(lastReason == "lava_burning");
	}

	void Test_HazardSimulator_DeathOnMaxDepth()
	{
		// Scène Quicksand avec escapeMode=None pour atteindre maxDepth sans escape.
		HazardScene s;
		HazardInstance h{ HazardType::Quicksand, HazardShape::Cylinder,
			Vec3{0, 0, 0}, Vec3{2, 1, 2}, 5.0f, 2.0f,
			0.5f, 1.0f, 0.10f, EscapeMode::None, 0 };
		s.hazards.push_back(h);

		HazardSimulator sim;
		int dieCount = 0;
		std::string lastReason;
		HazardCallbacks cb;
		cb.die = [&dieCount, &lastReason](std::string_view reason) {
			++dieCount;
			lastReason = std::string(reason);
		};
		sim.Init(s, cb);

		// 0.5 m/s × 0.1 s = 0.05 m/frame. maxDepth=1.0 → ~20 frames.
		for (int i = 0; i < 25; ++i)
		{
			sim.Update(0.1f, Vec3{0, 0.5f, 0}, false);
			if (dieCount > 0) break;
		}
		REQUIRE(dieCount == 1);
		REQUIRE(lastReason == "hazard_drowning");
	}
}

int main()
{
	Test_HazardSimulator_SinkRate();
	Test_HazardSimulator_MashEscape();
	Test_HazardSimulator_LateralEscape();
	Test_HazardSimulator_LavaKills3s();
	Test_HazardSimulator_DeathOnMaxDepth();
	if (g_failed == 0) std::printf("[OK] 5 tests passed\n");
	return g_failed == 0 ? 0 : 1;
}
