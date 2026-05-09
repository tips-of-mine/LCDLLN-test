// CMANGOS.17 (Phase 3.17a) — Tests LootTable roll deterministe.

#include "engine/server/loot/LootTable.h"
#include "engine/core/Log.h"

#include <random>

namespace
{
	using engine::server::loot::LootEntryItem;
	using engine::server::loot::LootEntryRef;
	using engine::server::loot::LootGroup;
	using engine::server::loot::LootRollItem;
	using engine::server::loot::LootTable;
	using engine::server::loot::LootTableRegistry;
	using engine::server::loot::RollLoot;

	bool TestIndependentItem100Pct()
	{
		LootTable t;
		t.name = "trash";
		t.independentItems.push_back({42, 1, 1, 100.0f});
		LootTableRegistry reg;
		std::mt19937 rng(123);

		auto out = RollLoot(t, reg, rng);
		if (out.size() != 1) return false;
		if (out[0].itemTemplateId != 42 || out[0].count != 1) return false;
		LOG_INFO(Core, "[LootTableTests] 100%% independent item OK");
		return true;
	}

	bool TestIndependentItem0Pct()
	{
		LootTable t;
		t.independentItems.push_back({42, 1, 1, 0.0f});
		LootTableRegistry reg;
		std::mt19937 rng(123);

		auto out = RollLoot(t, reg, rng);
		if (!out.empty()) return false;
		LOG_INFO(Core, "[LootTableTests] 0%% never rolls OK");
		return true;
	}

	bool TestRangedCount()
	{
		LootTable t;
		t.independentItems.push_back({99, 5, 10, 100.0f});
		LootTableRegistry reg;
		std::mt19937 rng(42);
		auto out = RollLoot(t, reg, rng);
		if (out.size() != 1) return false;
		if (out[0].count < 5 || out[0].count > 10) return false;
		LOG_INFO(Core, "[LootTableTests] ranged count OK (got {})", out[0].count);
		return true;
	}

	bool TestGroupPicksOne()
	{
		LootTable t;
		LootGroup g;
		g.items.push_back({1, 1, 1, 50.0f});
		g.items.push_back({2, 1, 1, 50.0f});
		g.items.push_back({3, 1, 1, 50.0f});
		t.groups.push_back(g);
		LootTableRegistry reg;
		// Plusieurs runs : on doit toujours avoir EXACTEMENT 1 item du groupe.
		for (uint32_t seed = 0; seed < 20; ++seed)
		{
			std::mt19937 rng(seed);
			auto out = RollLoot(t, reg, rng);
			if (out.size() != 1)
			{
				LOG_ERROR(Core, "[LootTableTests] group seed={} got {} items", seed, out.size());
				return false;
			}
		}
		LOG_INFO(Core, "[LootTableTests] group picks exactly one OK");
		return true;
	}

	bool TestRefChain()
	{
		LootTable child;
		child.name = "rare_drop";
		child.independentItems.push_back({100, 1, 1, 100.0f});

		LootTable parent;
		parent.name = "boss";
		parent.independentRefs.push_back({"rare_drop", 100.0f});
		parent.independentItems.push_back({1, 1, 1, 100.0f});

		LootTableRegistry reg;
		reg.Register(child);
		std::mt19937 rng(777);
		auto out = RollLoot(parent, reg, rng);
		// On attend 2 items : le 1 (parent) et le 100 (child).
		if (out.size() != 2) return false;
		bool foundParent = false, foundChild = false;
		for (auto& r : out)
		{
			if (r.itemTemplateId == 1) foundParent = true;
			if (r.itemTemplateId == 100) foundChild = true;
		}
		if (!foundParent || !foundChild) return false;
		LOG_INFO(Core, "[LootTableTests] ref chain OK");
		return true;
	}

	bool TestRefMaxDepth()
	{
		// A → A → A → ... infiniment. Doit s'arreter a kMaxRefDepth.
		LootTable t;
		t.name = "loop";
		t.independentRefs.push_back({"loop", 100.0f});
		t.independentItems.push_back({1, 1, 1, 100.0f});

		LootTableRegistry reg;
		reg.Register(t);
		std::mt19937 rng(1);
		auto out = RollLoot(t, reg, rng);
		// kMaxRefDepth=4 → A, A, A, A, A → 5 niveaux d'items max.
		// Avec depth start=0 : au depth 4, AppendFromRef refuse → on a
		// les items aux depths 0, 1, 2, 3 → 4 items.
		if (out.size() > 5)
		{
			LOG_ERROR(Core, "[LootTableTests] expected ≤ 5 items (depth cap), got {}", out.size());
			return false;
		}
		LOG_INFO(Core, "[LootTableTests] ref max depth honored OK (got {})", out.size());
		return true;
	}

	bool TestDeterminism()
	{
		LootTable t;
		t.independentItems.push_back({1, 1, 5, 50.0f});
		t.independentItems.push_back({2, 1, 5, 50.0f});
		LootTableRegistry reg;

		std::mt19937 rng1(12345);
		std::mt19937 rng2(12345);
		auto a = RollLoot(t, reg, rng1);
		auto b = RollLoot(t, reg, rng2);
		if (a.size() != b.size()) return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (a[i].itemTemplateId != b[i].itemTemplateId
				|| a[i].count != b[i].count) return false;
		}
		LOG_INFO(Core, "[LootTableTests] determinism (same seed = same result) OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestIndependentItem100Pct()
		&& TestIndependentItem0Pct()
		&& TestRangedCount()
		&& TestGroupPicksOne()
		&& TestRefChain()
		&& TestRefMaxDepth()
		&& TestDeterminism();

	if (ok) LOG_INFO(Core, "[LootTableTests] ALL OK");
	else LOG_ERROR(Core, "[LootTableTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
