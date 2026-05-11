// Wave 21 — Maps subclasses tests : WorldMap (open), DungeonMap (locked
// owner set + capacity), BattlegroundMap (capacity + scoreboard).
// Pattern aligne sur les autres tests entities/combat : asserts + printf.

#include "src/shardd/maps/Map.h"
#include "src/shardd/maps/WorldMap.h"
#include "src/shardd/maps/DungeonMap.h"
#include "src/shardd/maps/BattlegroundMap.h"

#include <cassert>
#include <cstdio>
#include <unordered_set>

using namespace engine::server::maps;

namespace
{
	// =========================================================================
	// Map base
	// =========================================================================

	void TestMapTypeToString()
	{
		assert(std::string(MapTypeToString(MapType::World)) == "World");
		assert(std::string(MapTypeToString(MapType::Dungeon)) == "Dungeon");
		assert(std::string(MapTypeToString(MapType::Battleground)) == "Battleground");
		assert(std::string(MapTypeToString(static_cast<MapType>(99))) == "Unknown");
		std::puts("[OK] TestMapTypeToString");
	}

	// =========================================================================
	// WorldMap
	// =========================================================================

	void TestWorldMapBasic()
	{
		WorldMap m(/*mapId*/1, /*instanceId*/100);
		assert(m.Id() == 1);
		assert(m.Instance() == 100);
		assert(m.Type() == MapType::World);
		assert(m.PlayerCount() == 0);
		std::puts("[OK] TestWorldMapBasic");
	}

	void TestWorldMapAcceptsAllPlayers()
	{
		WorldMap m(1, 100);
		// WorldMap accepte tout le monde sans lock.
		assert(m.AddPlayer(10));
		assert(m.AddPlayer(20));
		assert(m.AddPlayer(30));
		assert(m.PlayerCount() == 3);
		// Idempotent sur meme id.
		assert(m.AddPlayer(10));
		assert(m.PlayerCount() == 3);
		// Remove fonctionne.
		m.RemovePlayer(20);
		assert(m.PlayerCount() == 2);
		assert(!m.HasPlayer(20));
		std::puts("[OK] TestWorldMapAcceptsAllPlayers");
	}

	// =========================================================================
	// DungeonMap
	// =========================================================================

	void TestDungeonMapLockedOwnerSet()
	{
		std::unordered_set<uint64_t> owners = {1, 2, 3};
		DungeonMap m(/*mapId*/10, /*instanceId*/200, owners);
		assert(m.Type() == MapType::Dungeon);

		// Players du owner set : OK.
		assert(m.AddPlayer(1));
		assert(m.AddPlayer(2));
		assert(m.AddPlayer(3));
		assert(m.PlayerCount() == 3);

		// Player hors owner set : REJET.
		assert(!m.AddPlayer(4));
		assert(!m.AddPlayer(99));
		assert(m.PlayerCount() == 3);
		std::puts("[OK] TestDungeonMapLockedOwnerSet");
	}

	void TestDungeonMapCapacity()
	{
		std::unordered_set<uint64_t> owners = {1, 2, 3, 4, 5, 6};
		// Capacite 3 < 6 owners -> les 3 premiers passent, les autres non.
		DungeonMap m(10, 200, owners, /*maxCapacity*/3);

		assert(m.AddPlayer(1));
		assert(m.AddPlayer(2));
		assert(m.AddPlayer(3));
		// Maintenant a la cap.
		assert(!m.AddPlayer(4));
		assert(!m.AddPlayer(5));
		assert(m.PlayerCount() == 3);

		// Re-add d'un deja present : idempotent (pas de slot consomme).
		assert(m.AddPlayer(1));
		assert(m.PlayerCount() == 3);
		std::puts("[OK] TestDungeonMapCapacity");
	}

	void TestDungeonMapNoCapacityLimit()
	{
		std::unordered_set<uint64_t> owners = {1, 2, 3};
		// Pas de capacite (default 0) -> tous les owners passent.
		DungeonMap m(10, 200, owners);
		assert(m.AddPlayer(1));
		assert(m.AddPlayer(2));
		assert(m.AddPlayer(3));
		assert(m.PlayerCount() == 3);
		std::puts("[OK] TestDungeonMapNoCapacityLimit");
	}

	// =========================================================================
	// BattlegroundMap
	// =========================================================================

	void TestBattlegroundMapCapacity()
	{
		// Capacite 4 (2v2 mini-BG)
		BattlegroundMap m(/*mapId*/30, /*instanceId*/300, /*maxCapacity*/4);
		assert(m.Type() == MapType::Battleground);

		assert(m.AddPlayer(1));
		assert(m.AddPlayer(2));
		assert(m.AddPlayer(3));
		assert(m.AddPlayer(4));
		assert(m.PlayerCount() == 4);

		// Cap atteinte.
		assert(!m.AddPlayer(5));
		assert(m.PlayerCount() == 4);

		// Idempotent.
		assert(m.AddPlayer(1));
		assert(m.PlayerCount() == 4);
		std::puts("[OK] TestBattlegroundMapCapacity");
	}

	void TestBattlegroundMapScoreboard()
	{
		BattlegroundMap m(30, 300, 10);
		assert(m.AddPlayer(1));
		assert(m.AddPlayer(2));

		m.AddKill(1);
		m.AddKill(1);
		m.AddDeath(2);
		m.AddHonor(1, 100);
		m.AddHonor(2, 25);

		auto s1 = m.ScoreOf(1);
		assert(s1.kills == 2);
		assert(s1.deaths == 0);
		assert(s1.honorPoints == 100);

		auto s2 = m.ScoreOf(2);
		assert(s2.kills == 0);
		assert(s2.deaths == 1);
		assert(s2.honorPoints == 25);

		// Player jamais entre -> score vide.
		auto s99 = m.ScoreOf(99);
		assert(s99.kills == 0 && s99.deaths == 0 && s99.honorPoints == 0);
		std::puts("[OK] TestBattlegroundMapScoreboard");
	}

	void TestBattlegroundMapScorePersistsAfterLeave()
	{
		BattlegroundMap m(30, 300, 10);
		assert(m.AddPlayer(1));
		m.AddKill(1);
		m.AddHonor(1, 50);
		// Le player quitte avant la fin.
		m.RemovePlayer(1);
		assert(!m.HasPlayer(1));
		// Mais son score reste accessible (utile pour scoreboard post-match).
		auto s = m.ScoreOf(1);
		assert(s.kills == 1);
		assert(s.honorPoints == 50);
		std::puts("[OK] TestBattlegroundMapScorePersistsAfterLeave");
	}

	void TestBattlegroundMapScoreOnNonInitialized()
	{
		BattlegroundMap m(30, 300, 10);
		// AddKill sur un player qui n'a jamais AddPlayer -> no-op silent.
		m.AddKill(99);
		auto s = m.ScoreOf(99);
		assert(s.kills == 0);  // pas d'init = pas de score
		std::puts("[OK] TestBattlegroundMapScoreOnNonInitialized");
	}
}

int main()
{
	TestMapTypeToString();
	TestWorldMapBasic();
	TestWorldMapAcceptsAllPlayers();
	TestDungeonMapLockedOwnerSet();
	TestDungeonMapCapacity();
	TestDungeonMapNoCapacityLimit();
	TestBattlegroundMapCapacity();
	TestBattlegroundMapScoreboard();
	TestBattlegroundMapScorePersistsAfterLeave();
	TestBattlegroundMapScoreOnNonInitialized();
	std::puts("All Maps subclasses tests passed");
	return 0;
}
