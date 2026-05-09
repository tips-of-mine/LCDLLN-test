/// Tests unitaires pour ChunkRuntime (cache LRU pure CPU, pas de Vulkan).
///
/// Vérifient :
///   - GetOrAllocateSlot idempotent (même coord → même slot).
///   - AddResidentBytes accumule la résidence trackée.
///   - Visible ring jamais évincé (protection).
///   - LRU eviction par ordre d'usage (Touch déplace en tête).
///
/// Style aligné sur src/client/world/terrain/tests/TerrainChunkTests.cpp
/// (REQUIRE macro maison + main() appelant chaque test function).

#include "src/client/render/terrain_chunk/ChunkRuntime.h"

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

	using engine::render::terrain_chunk::ChunkRuntime;
	using engine::render::terrain_chunk::ChunkResidency;
	using engine::render::terrain_chunk::ChunkSlotId;
	using engine::world::ChunkRing;
	using engine::world::GlobalChunkCoord;

	/// Slot alloué une fois est stable (idempotence).
	void Test_GetOrAllocateSlot_Idempotent()
	{
		ChunkRuntime rt;
		rt.Init({});
		auto s1 = rt.GetOrAllocateSlot({3, 5});
		auto s2 = rt.GetOrAllocateSlot({3, 5});
		REQUIRE(s1 == s2);
		REQUIRE(rt.GetSlotCount() == 1u);
	}

	/// AddResidentBytes accumule + GetResidentBytes le reflète.
	void Test_AddResidentBytes_Accumulates()
	{
		ChunkRuntime rt;
		rt.Init({});
		auto s = rt.GetOrAllocateSlot({0, 0});
		rt.AddResidentBytes(s, 1000);
		rt.AddResidentBytes(s, 500);
		REQUIRE(rt.GetResidentBytes() == 1500u);
		REQUIRE(rt.GetResidency(s) == ChunkResidency::Resident);
	}

	/// Visible ring ne peut pas être évincé même si budget dépassé.
	void Test_VisibleRing_NeverEvicted()
	{
		ChunkRuntime::Config cfg;
		cfg.gpuBudgetBytes = 1000;
		ChunkRuntime rt;
		rt.Init(cfg);
		auto sFar = rt.GetOrAllocateSlot({10, 10});
		rt.UpdateRing({10, 10}, ChunkRing::Far);
		rt.AddResidentBytes(sFar, 500);
		auto sVis = rt.GetOrAllocateSlot({0, 0});
		rt.UpdateRing({0, 0}, ChunkRing::Visible);
		rt.AddResidentBytes(sVis, 800); // total 1300 > budget 1000

		auto evictions = rt.CollectEvictionsForBudget();
		// Far doit être candidat, Visible doit être protégé.
		REQUIRE(evictions.size() == 1u);
		REQUIRE(evictions[0] == sFar);
	}

	/// LRU : chunk le moins récemment touché évincé en premier.
	void Test_LruEviction_ByTouchOrder()
	{
		ChunkRuntime::Config cfg;
		cfg.gpuBudgetBytes = 100;
		ChunkRuntime rt;
		rt.Init(cfg);
		auto sA = rt.GetOrAllocateSlot({1, 0});
		auto sB = rt.GetOrAllocateSlot({2, 0});
		rt.UpdateRing({1, 0}, ChunkRing::Far);
		rt.UpdateRing({2, 0}, ChunkRing::Far);
		rt.AddResidentBytes(sA, 60);
		rt.AddResidentBytes(sB, 60); // total 120 > 100
		// On touche sA pour le rendre plus récent que sB → sB devrait être évincé.
		rt.Touch(sA);
		auto evictions = rt.CollectEvictionsForBudget();
		REQUIRE(evictions.size() >= 1u);
		REQUIRE(evictions[0] == sB);
	}
}

int main()
{
	Test_GetOrAllocateSlot_Idempotent();
	Test_AddResidentBytes_Accumulates();
	Test_VisibleRing_NeverEvicted();
	Test_LruEviction_ByTouchOrder();

	if (g_failed == 0)
	{
		std::printf("[PASS] ChunkRuntimeTests (4/4)\n");
		return 0;
	}
	std::printf("[FAIL] ChunkRuntimeTests: %d failure(s)\n", g_failed);
	return 1;
}
