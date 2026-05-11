// Wave 17 entities tests : Player (accountId, characterId, name, xp + heritage Unit).
// Pattern aligne sur ObjectGuidTests.cpp : asserts + printf, pas de framework.
// Cible CTest : player_tests (cf. src/CMakeLists.txt).

#include "src/shardd/entities/Player.h"

#include <cassert>
#include <cstdio>
#include <string>

using namespace engine::server::entities;

namespace
{
	/// Construction : guid, accountId, characterId, name preserves.
	void TestPlayerConstruction()
	{
		ObjectGuid g(ObjectType::Player, 42);
		Player p(g, /*accountId*/1001, /*characterId*/42, /*name*/"TestHero");
		assert(p.Guid() == g);
		assert(p.GetAccountId() == 1001);
		assert(p.GetCharacterId() == 42);
		assert(p.GetName() == "TestHero");
		std::puts("[OK] TestPlayerConstruction");
	}

	/// Name : immuable apres construction (pas de SetName). Verification
	/// via API : seul GetName() existe.
	void TestPlayerNameImmutable()
	{
		ObjectGuid g(ObjectType::Player, 1);
		Player p(g, 1, 1, "Alpha");
		const std::string& n = p.GetName();
		assert(n == "Alpha");
		// Pas d'API SetName : compile-time invariant.
		std::puts("[OK] TestPlayerNameImmutable");
	}

	/// Account/Character ids sont MarkDirty() a la construction : la premiere
	/// replication les envoie au client.
	void TestPlayerIdsMarkedDirtyAtConstruction()
	{
		ObjectGuid g(ObjectType::Player, 9);
		Player p(g, 1234, 5678, "DirtyTest");
		assert(p.IsDirty());
		assert(p.Mask().TestBit(kPlayerFieldAccountId));
		assert(p.Mask().TestBit(kPlayerFieldCharacterId));
		// XP n'a pas ete touche.
		assert(!p.Mask().TestBit(kPlayerFieldXp));
		std::puts("[OK] TestPlayerIdsMarkedDirtyAtConstruction");
	}

	/// SetXp marque le bit XP, sans toucher aux autres.
	void TestPlayerXp()
	{
		ObjectGuid g(ObjectType::Player, 2);
		Player p(g, 1, 2, "Beta");
		p.OnReplicationSent(); // reset mask post-construction (clear account/char marks)
		p.SetXp(12345);
		assert(p.GetXp() == 12345);
		assert(p.Mask().TestBit(kPlayerFieldXp));
		// Seul XP dirty, pas account/char.
		assert(p.Mask().PopCount() == 1);
		std::puts("[OK] TestPlayerXp");
	}

	/// Player herite de Unit : HP, level, faction fonctionnent.
	void TestPlayerHeriteUnit()
	{
		ObjectGuid g(ObjectType::Player, 3);
		Player p(g, 1, 3, "Gamma");
		p.SetMaxHealth(5000);
		p.SetHealth(3000);
		p.SetLevel(60);
		p.SetFaction(1); // exemple faction joueur
		assert(p.GetHealth() == 3000);
		assert(p.GetLevel() == 60);
		assert(p.GetFaction() == 1);
		assert(p.IsAlive());
		std::puts("[OK] TestPlayerHeriteUnit");
	}

	/// Player herite de WorldObject : position fonctionne.
	void TestPlayerHeriteWorldObject()
	{
		ObjectGuid g(ObjectType::Player, 4);
		Player p(g, 1, 4, "Delta");
		p.SetPosition(100.0f, 200.0f, 30.0f, 1.0f);
		p.SetMapId(1);
		assert(p.GetPosX() == 100.0f);
		assert(p.GetMapId() == 1);
		std::puts("[OK] TestPlayerHeriteWorldObject");
	}
}

int main()
{
	TestPlayerConstruction();
	TestPlayerNameImmutable();
	TestPlayerIdsMarkedDirtyAtConstruction();
	TestPlayerXp();
	TestPlayerHeriteUnit();
	TestPlayerHeriteWorldObject();
	std::puts("All Player tests passed");
	return 0;
}
