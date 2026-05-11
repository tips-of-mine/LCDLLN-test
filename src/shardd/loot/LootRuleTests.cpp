// Wave 22 — LootRule tests : 4 implementations (FFA, RoundRobin,
// MasterLooter, NeedBeforeGreed).
//
// Pattern aligne sur les autres tests : asserts + printf, pas de framework.
// Cible CTest : loot_rule_tests.

#include "src/shardd/loot/LootRule.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::loot;
using engine::server::groups::PlayerId;

namespace
{
	// ========================================================================
	// FreeForAll
	// ========================================================================

	void TestFFAFirstEligible()
	{
		FreeForAllLootRule rule;
		std::vector<PlayerId> eligible = {1, 2, 3};
		auto winner = rule.Pick(eligible, {});
		assert(winner.has_value() && winner.value() == 1);
		std::puts("[OK] TestFFAFirstEligible");
	}

	void TestFFAEmpty()
	{
		FreeForAllLootRule rule;
		auto winner = rule.Pick({}, {});
		assert(!winner.has_value());
		std::puts("[OK] TestFFAEmpty");
	}

	// ========================================================================
	// RoundRobin
	// ========================================================================

	void TestRRRotation()
	{
		RoundRobinLootRule rule;
		std::vector<PlayerId> eligible = {1, 2, 3};
		// 1er pick : index 0 = player 1
		auto w1 = rule.Pick(eligible, {});
		assert(w1.has_value() && w1.value() == 1);
		// 2e : index 1 = player 2
		auto w2 = rule.Pick(eligible, {});
		assert(w2.has_value() && w2.value() == 2);
		// 3e : index 2 = player 3
		auto w3 = rule.Pick(eligible, {});
		assert(w3.has_value() && w3.value() == 3);
		// 4e : retour a index 0 = player 1
		auto w4 = rule.Pick(eligible, {});
		assert(w4.has_value() && w4.value() == 1);
		std::puts("[OK] TestRRRotation");
	}

	void TestRRCursorState()
	{
		RoundRobinLootRule rule;
		std::vector<PlayerId> eligible = {10, 20, 30};
		rule.SetCursor(2);
		auto w = rule.Pick(eligible, {});
		// cursor=2 % 3 = 2 -> player 30
		assert(w.has_value() && w.value() == 30);
		assert(rule.GetCursor() == 3);
		std::puts("[OK] TestRRCursorState");
	}

	void TestRREmpty()
	{
		RoundRobinLootRule rule;
		auto w = rule.Pick({}, {});
		assert(!w.has_value());
		std::puts("[OK] TestRREmpty");
	}

	// ========================================================================
	// MasterLooter
	// ========================================================================

	void TestMLLeaderWins()
	{
		MasterLooterLootRule rule(/*masterLooter*/5);
		std::vector<PlayerId> eligible = {1, 2, 5, 7};
		auto w = rule.Pick(eligible, {});
		assert(w.has_value() && w.value() == 5);
		std::puts("[OK] TestMLLeaderWins");
	}

	void TestMLFallbackWhenOffline()
	{
		// Si masterLooter n'est pas dans eligible (offline / kick), fallback
		// au premier (defense).
		MasterLooterLootRule rule(/*masterLooter*/99);
		std::vector<PlayerId> eligible = {1, 2, 3};
		auto w = rule.Pick(eligible, {});
		assert(w.has_value() && w.value() == 1);
		std::puts("[OK] TestMLFallbackWhenOffline");
	}

	void TestMLEmpty()
	{
		MasterLooterLootRule rule(5);
		auto w = rule.Pick({}, {});
		assert(!w.has_value());
		std::puts("[OK] TestMLEmpty");
	}

	// ========================================================================
	// NeedBeforeGreed
	// ========================================================================

	void TestNBGNeedWins()
	{
		NeedBeforeGreedLootRule rule;
		std::vector<PlayerId> eligible = {1, 2, 3};
		std::unordered_map<PlayerId, RollChoice> rolls = {
			{1, RollChoice::Greed},
			{2, RollChoice::Need},  // priorite haute
			{3, RollChoice::Greed},
		};
		auto w = rule.Pick(eligible, rolls);
		assert(w.has_value() && w.value() == 2);
		std::puts("[OK] TestNBGNeedWins");
	}

	void TestNBGGreedFallback()
	{
		NeedBeforeGreedLootRule rule;
		std::vector<PlayerId> eligible = {1, 2, 3};
		std::unordered_map<PlayerId, RollChoice> rolls = {
			{1, RollChoice::Pass},
			{2, RollChoice::Greed},
			{3, RollChoice::Pass},
		};
		auto w = rule.Pick(eligible, rolls);
		assert(w.has_value() && w.value() == 2);
		std::puts("[OK] TestNBGGreedFallback");
	}

	void TestNBGAllPass()
	{
		NeedBeforeGreedLootRule rule;
		std::vector<PlayerId> eligible = {1, 2, 3};
		std::unordered_map<PlayerId, RollChoice> rolls = {
			{1, RollChoice::Pass},
			{2, RollChoice::Pass},
			{3, RollChoice::Pass},
		};
		auto w = rule.Pick(eligible, rolls);
		assert(!w.has_value());
		std::puts("[OK] TestNBGAllPass");
	}

	void TestNBGNeedTiebreakerFirstInList()
	{
		NeedBeforeGreedLootRule rule;
		// 2 players Need -> celui qui apparait premier dans eligible gagne.
		std::vector<PlayerId> eligible = {1, 2, 3};
		std::unordered_map<PlayerId, RollChoice> rolls = {
			{1, RollChoice::Need},
			{2, RollChoice::Need},
			{3, RollChoice::Greed},
		};
		auto w = rule.Pick(eligible, rolls);
		assert(w.has_value() && w.value() == 1);
		std::puts("[OK] TestNBGNeedTiebreakerFirstInList");
	}

	void TestNBGMissingRollsCountAsPass()
	{
		NeedBeforeGreedLootRule rule;
		// Si un player n'a pas roll, il est traite comme Pass (pas dans la map).
		std::vector<PlayerId> eligible = {1, 2};
		std::unordered_map<PlayerId, RollChoice> rolls = {
			{1, RollChoice::Greed},
			// player 2 absent
		};
		auto w = rule.Pick(eligible, rolls);
		assert(w.has_value() && w.value() == 1);  // 1 Greed gagne
		std::puts("[OK] TestNBGMissingRollsCountAsPass");
	}
}

int main()
{
	TestFFAFirstEligible();
	TestFFAEmpty();
	TestRRRotation();
	TestRRCursorState();
	TestRREmpty();
	TestMLLeaderWins();
	TestMLFallbackWhenOffline();
	TestMLEmpty();
	TestNBGNeedWins();
	TestNBGGreedFallback();
	TestNBGAllPass();
	TestNBGNeedTiebreakerFirstInList();
	TestNBGMissingRollsCountAsPass();
	std::puts("All LootRule tests passed");
	return 0;
}
