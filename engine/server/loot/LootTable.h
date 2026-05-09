#pragma once
// CMANGOS.17 (Phase 3.17a) — LootTable + roll deterministe (testable
// via seed). Templates DB + groups + reference (chained tables). Pas
// encore de DB persistence ; pure data + algorithm.

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace engine::server::loot
{
	/// Resultat de roll : item template + count.
	struct LootRollItem
	{
		uint32_t itemTemplateId = 0;
		uint32_t count          = 1;
	};

	/// Une entree directe (item simple).
	struct LootEntryItem
	{
		uint32_t itemTemplateId = 0;
		uint32_t minCount       = 1;
		uint32_t maxCount       = 1;
		float    chance         = 100.0f;  ///< 0..100 (%).
	};

	/// Une reference vers une autre table (chained loot).
	struct LootEntryRef
	{
		std::string referencedTable;
		float       chance = 100.0f;
	};

	/// Un groupe : on roll exactement 1 entree parmi le groupe selon
	/// les `chance` ponderees.
	struct LootGroup
	{
		std::vector<LootEntryItem> items;
		std::vector<LootEntryRef>  refs;
	};

	struct LootTable
	{
		std::string name;
		/// Entries independantes : chacune roll independamment selon sa chance.
		std::vector<LootEntryItem> independentItems;
		std::vector<LootEntryRef>  independentRefs;
		/// Groupes : 1 entree par groupe (tirage pondere).
		std::vector<LootGroup>     groups;
	};

	/// Registry de toutes les tables connues. Le caller load + register
	/// avant de Roll.
	class LootTableRegistry
	{
	public:
		void Register(LootTable table)
		{
			m_tables[table.name] = std::move(table);
		}

		const LootTable* Find(const std::string& name) const
		{
			auto it = m_tables.find(name);
			return (it == m_tables.end()) ? nullptr : &it->second;
		}

		size_t Size() const noexcept { return m_tables.size(); }

	private:
		std::unordered_map<std::string, LootTable> m_tables;
	};

	/// Roll deterministe sur une table. \p rng doit etre seed par le
	/// caller (typiquement std::mt19937 avec un seed = entityId XOR
	/// nowMs pour la reproductibilite anti-cheat).
	///
	/// Retourne tous les items rolled (vide si aucun ne passe).
	/// Anti-recursion : profondeur max kMaxRefDepth pour les ref chains.
	std::vector<LootRollItem> RollLoot(const LootTable& table,
		const LootTableRegistry& registry,
		std::mt19937& rng,
		size_t depth = 0);

	inline constexpr size_t kMaxRefDepth = 4;
}
