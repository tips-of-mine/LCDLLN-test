// M100.16 — Tests hazards : round-trip binaire + comportements du simulateur.
// Headless. Lié à engine_core (HazardSimulator) ; format header-only.

#include "src/client/world/hazard/HazardSimulator.h"
#include "src/client/world/hazard/HazardVolumes.h"

#include <cstdio>
#include <vector>

using namespace engine::world::hazard;

namespace
{
	int g_failed = 0;

#define REQUIRE(cond) do { \
	if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
		++g_failed; \
	} \
} while (0)

	bool Near(float a, float b, float eps = 1e-4f) { return (a - b < eps) && (b - a < eps); }

	void Test_Hazards_RoundtripBin()
	{
		std::vector<HazardVolume> hz;
		HazardVolume q = MakeDefaultHazard(HazardType::Quicksand); q.position = { 1.0f, 2.0f, 3.0f };
		HazardVolume b = MakeDefaultHazard(HazardType::Bog);       b.shape = HazardShape::Box; b.boxHalfExtents = { 3.0f, 1.0f, 2.0f };
		HazardVolume t = MakeDefaultHazard(HazardType::Tar);       t.requiredItemId = 42u;
		HazardVolume l = MakeDefaultHazard(HazardType::LavaSurface);
		hz = { q, b, t, l };

		std::vector<uint8_t> bytes = SaveHazardsBin(hz);
		std::vector<HazardVolume> out;
		std::string err;
		REQUIRE(LoadHazardsBin(bytes, out, err));
		REQUIRE(out.size() == 4);
		if (out.size() == 4)
		{
			REQUIRE(out[0].type == HazardType::Quicksand);
			REQUIRE(Near(out[0].position.x, 1.0f) && Near(out[0].position.z, 3.0f));
			REQUIRE(out[1].shape == HazardShape::Box);
			REQUIRE(Near(out[1].boxHalfExtents.x, 3.0f));
			REQUIRE(out[2].type == HazardType::Tar);
			REQUIRE(out[2].requiredItemId == 42u);
			REQUIRE(out[2].escapeMode == EscapeMode::MashButtonRequireItem);
			REQUIRE(out[3].type == HazardType::LavaSurface);
		}
	}

	void Test_HazardSimulator_SinkRate()
	{
		HazardSimulator sim;
		sim.Enter(MakeDefaultHazard(HazardType::Quicksand)); // sinkRate 0.15
		HazardInput in; in.dtSeconds = 1.0f;
		HazardOutput out = sim.Update(in);
		REQUIRE(out.state == HazardState::Sinking);
		REQUIRE(Near(out.currentDepth, 0.15f));
		REQUIRE(Near(out.groundOffsetMeters, 0.15f));
	}

	void Test_HazardSimulator_MashEscape()
	{
		HazardSimulator sim;
		sim.Enter(MakeDefaultHazard(HazardType::Quicksand));
		HazardInput in; in.dtSeconds = 0.1f; in.actionPressed = true;
		HazardState last = HazardState::Sinking;
		for (int i = 0; i < 10; ++i) last = sim.Update(in).state;
		REQUIRE(last == HazardState::Escaped);
	}

	void Test_HazardSimulator_LateralEscape()
	{
		HazardSimulator sim;
		sim.Enter(MakeDefaultHazard(HazardType::Bog)); // LateralMove, 2 m
		HazardInput in; in.dtSeconds = 0.1f; in.lateralDeltaMeters = 0.5f;
		HazardState last = HazardState::Sinking;
		for (int i = 0; i < 4; ++i) last = sim.Update(in).state; // 4 * 0.5 = 2.0 m
		REQUIRE(last == HazardState::Escaped);
	}

	void Test_HazardSimulator_LavaKills3s()
	{
		HazardSimulator sim;
		sim.Enter(MakeDefaultHazard(HazardType::LavaSurface));
		HazardInput in; in.dtSeconds = 0.5f;
		for (int i = 0; i < 5; ++i) sim.Update(in); // 2.5 s
		REQUIRE(sim.GetState() == HazardState::Sinking);
		HazardOutput out = sim.Update(in); // 3.0 s
		REQUIRE(out.state == HazardState::Dead);
		REQUIRE(out.deathReason == "lava_burning");
	}

	void Test_HazardSimulator_DeathOnMaxDepth()
	{
		HazardVolume v = MakeDefaultHazard(HazardType::Quicksand);
		v.escapeMode = EscapeMode::None; // pas d'évasion possible
		HazardSimulator sim;
		sim.Enter(v);
		HazardInput in; in.dtSeconds = 1.0f; // 0.15 m/s
		HazardOutput out;
		for (int i = 0; i < 12; ++i) out = sim.Update(in); // 12 * 0.15 = 1.8 m
		REQUIRE(out.state == HazardState::Dead);
		REQUIRE(out.deathReason == "hazard_drowning");
	}
}

int main()
{
	Test_Hazards_RoundtripBin();
	Test_HazardSimulator_SinkRate();
	Test_HazardSimulator_MashEscape();
	Test_HazardSimulator_LateralEscape();
	Test_HazardSimulator_LavaKills3s();
	Test_HazardSimulator_DeathOnMaxDepth();

	if (g_failed == 0)
		std::fprintf(stderr, "[OK] HazardTests: tous les tests passent\n");
	else
		std::fprintf(stderr, "[FAIL] HazardTests: %d échec(s)\n", g_failed);
	return g_failed;
}
