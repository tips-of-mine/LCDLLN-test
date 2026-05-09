#include "engine/server/loot/LootTable.h"

namespace engine::server::loot
{
	namespace
	{
		LootRollItem RollItem(const LootEntryItem& e, std::mt19937& rng)
		{
			LootRollItem r;
			r.itemTemplateId = e.itemTemplateId;
			if (e.maxCount > e.minCount)
			{
				std::uniform_int_distribution<uint32_t> d(e.minCount, e.maxCount);
				r.count = d(rng);
			}
			else
			{
				r.count = e.minCount;
			}
			return r;
		}

		bool RollChance(float pct, std::mt19937& rng)
		{
			if (pct >= 100.0f) return true;
			if (pct <= 0.0f) return false;
			std::uniform_real_distribution<float> d(0.0f, 100.0f);
			return d(rng) < pct;
		}

		void AppendFromRef(const LootEntryRef& ref,
			const LootTableRegistry& registry,
			std::mt19937& rng, size_t depth,
			std::vector<LootRollItem>& out)
		{
			if (depth >= kMaxRefDepth) return;
			if (!RollChance(ref.chance, rng)) return;
			const auto* sub = registry.Find(ref.referencedTable);
			if (!sub) return;
			auto inner = RollLoot(*sub, registry, rng, depth + 1);
			out.insert(out.end(), inner.begin(), inner.end());
		}
	}

	std::vector<LootRollItem> RollLoot(const LootTable& table,
		const LootTableRegistry& registry,
		std::mt19937& rng,
		size_t depth)
	{
		std::vector<LootRollItem> out;

		// 1. Independent items : chacun roll selon sa chance.
		for (const auto& e : table.independentItems)
		{
			if (RollChance(e.chance, rng))
				out.push_back(RollItem(e, rng));
		}

		// 2. Independent refs.
		for (const auto& r : table.independentRefs)
			AppendFromRef(r, registry, rng, depth, out);

		// 3. Groups : roulette pondere ; 1 entree par groupe (item OU ref).
		for (const auto& g : table.groups)
		{
			float totalWeight = 0.0f;
			for (const auto& e : g.items) totalWeight += e.chance;
			for (const auto& r : g.refs)  totalWeight += r.chance;
			if (totalWeight <= 0.0f) continue;

			std::uniform_real_distribution<float> d(0.0f, totalWeight);
			float pick = d(rng);
			float acc = 0.0f;
			bool picked = false;
			for (const auto& e : g.items)
			{
				acc += e.chance;
				if (pick <= acc)
				{
					out.push_back(RollItem(e, rng));
					picked = true;
					break;
				}
			}
			if (picked) continue;
			for (const auto& r : g.refs)
			{
				acc += r.chance;
				if (pick <= acc)
				{
					AppendFromRef(r, registry, rng, depth, out);
					break;
				}
			}
		}

		return out;
	}
}
