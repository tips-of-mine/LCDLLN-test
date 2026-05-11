// Wave 17 entities tests : Creature (templateEntry, spawnId + heritage Unit).
// Pattern aligne sur ObjectGuidTests.cpp : asserts + printf, pas de framework.
// Cible CTest : creature_tests (cf. src/CMakeLists.txt).

#include "src/shardd/entities/Creature.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::entities;

namespace
{
	/// Construction : guid + templateEntry + spawnId preserves.
	void TestCreatureConstruction()
	{
		ObjectGuid g(ObjectType::Creature, 100);
		Creature c(g, /*templateEntry*/666, /*spawnId*/12345);
		assert(c.Guid() == g);
		assert(c.GetTemplateEntry() == 666);
		assert(c.GetSpawnId() == 12345);
		std::puts("[OK] TestCreatureConstruction");
	}

	/// templateEntry immuable : pas de SetTemplateEntry. Compile-time invariant.
	void TestCreatureTemplateImmutable()
	{
		ObjectGuid g(ObjectType::Creature, 101);
		Creature c(g, 999, 1);
		assert(c.GetTemplateEntry() == 999);
		// Pas d'API SetTemplateEntry.
		std::puts("[OK] TestCreatureTemplateImmutable");
	}

	/// Ids marked dirty a la construction pour replication initiale.
	void TestCreatureIdsMarkedDirtyAtConstruction()
	{
		ObjectGuid g(ObjectType::Creature, 102);
		Creature c(g, 500, 99);
		assert(c.IsDirty());
		assert(c.Mask().TestBit(kCreatureFieldTemplateEntry));
		assert(c.Mask().TestBit(kCreatureFieldSpawnId));
		std::puts("[OK] TestCreatureIdsMarkedDirtyAtConstruction");
	}

	/// Heritage Unit : HP, MP, level, faction fonctionnent.
	void TestCreatureHeriteUnit()
	{
		ObjectGuid g(ObjectType::Creature, 103);
		Creature c(g, 500, 1);
		c.SetMaxHealth(2000);
		c.SetHealth(1500);
		c.SetFaction(85); // exemple : faction monstre
		c.SetLevel(40);
		assert(c.GetHealth() == 1500);
		assert(c.GetFaction() == 85);
		assert(c.GetLevel() == 40);
		assert(c.IsAlive());
		std::puts("[OK] TestCreatureHeriteUnit");
	}

	/// Death : HP = 0 → IsAlive = false.
	void TestCreatureDeath()
	{
		ObjectGuid g(ObjectType::Creature, 104);
		Creature c(g, 500, 1);
		c.SetMaxHealth(100);
		c.SetHealth(100);
		assert(c.IsAlive());
		c.SetHealth(0);
		assert(!c.IsAlive());
		std::puts("[OK] TestCreatureDeath");
	}

	/// Heritage WorldObject : position, map, zone fonctionnent.
	void TestCreatureHeriteWorldObject()
	{
		ObjectGuid g(ObjectType::Creature, 105);
		Creature c(g, 500, 1);
		c.SetPosition(50.0f, 60.0f, 70.0f, 3.14f);
		c.SetMapId(1);
		c.SetZoneId(42);
		assert(c.GetPosX() == 50.0f);
		assert(c.GetMapId() == 1);
		assert(c.GetZoneId() == 42);
		std::puts("[OK] TestCreatureHeriteWorldObject");
	}
}

int main()
{
	TestCreatureConstruction();
	TestCreatureTemplateImmutable();
	TestCreatureIdsMarkedDirtyAtConstruction();
	TestCreatureHeriteUnit();
	TestCreatureDeath();
	TestCreatureHeriteWorldObject();
	std::puts("All Creature tests passed");
	return 0;
}
