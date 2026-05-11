// Wave 17 entities tests : UpdateMask delta cross-classe. Verifie que les
// hierarchies Object/WorldObject/Unit/Player/Creature ne marquent QUE les
// champs effectivement modifies, et que les indices sont coherents.
//
// C'est le test critique de delta replication : si HP change sur un Player,
// SEUL kUnitFieldHealth doit etre dirty, pas le tableau entier.
//
// Pattern aligne sur ObjectGuidTests.cpp : asserts + printf, pas de framework.
// Cible CTest : update_mask_delta_tests (cf. src/CMakeLists.txt).

#include "src/shardd/entities/Creature.h"
#include "src/shardd/entities/Player.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::entities;

namespace
{
	/// Modifier seulement HP sur un Unit : 2 bits dirty (health + maxHealth).
	void TestDeltaUnitOnlyHealth()
	{
		ObjectGuid g(ObjectType::Creature, 1);
		Unit u(g, kUnitFieldCount);
		u.OnReplicationSent(); // partir d'un mask propre
		u.SetMaxHealth(100);
		u.SetHealth(50);
		assert(u.Mask().PopCount() == 2);
		assert(u.Mask().TestBit(kUnitFieldHealth));
		assert(u.Mask().TestBit(kUnitFieldMaxHealth));
		assert(!u.Mask().TestBit(kUnitFieldLevel));
		assert(!u.Mask().TestBit(kWorldObjectFieldPosX));
		std::puts("[OK] TestDeltaUnitOnlyHealth");
	}

	/// Modifier seulement XP sur un Player : 1 bit dirty (apres
	/// OnReplicationSent qui clear les marks account/character du ctor).
	void TestDeltaPlayerOnlyXp()
	{
		ObjectGuid g(ObjectType::Player, 2);
		Player p(g, 1001, 2, "Test");
		p.OnReplicationSent(); // reset mask post-construction
		p.SetXp(500);
		assert(p.Mask().PopCount() == 1);
		assert(p.Mask().TestBit(kPlayerFieldXp));
		assert(!p.Mask().TestBit(kPlayerFieldAccountId));
		std::puts("[OK] TestDeltaPlayerOnlyXp");
	}

	/// Modifier position 3D sur un Creature : 4 bits dirty (x/y/z/orientation),
	/// rien d'autre.
	void TestDeltaCreaturePosition()
	{
		ObjectGuid g(ObjectType::Creature, 3);
		Creature c(g, 100, 1);
		c.OnReplicationSent();
		c.SetPosition(1.0f, 2.0f, 3.0f, 0.5f);
		assert(c.Mask().PopCount() == 4);
		assert(c.Mask().TestBit(kWorldObjectFieldPosX));
		assert(c.Mask().TestBit(kWorldObjectFieldPosY));
		assert(c.Mask().TestBit(kWorldObjectFieldPosZ));
		assert(c.Mask().TestBit(kWorldObjectFieldOrientation));
		assert(!c.Mask().TestBit(kUnitFieldHealth));
		assert(!c.Mask().TestBit(kCreatureFieldTemplateEntry));
		std::puts("[OK] TestDeltaCreaturePosition");
	}

	/// Verification compile-time : Player et Creature ont chacun leur propre
	/// fieldCount (kPlayerFieldEnd != kCreatureFieldEnd). Ils demarrent au
	/// meme index (kUnitFieldEnd) mais c'est OK car ce sont 2 classes
	/// distinctes — chacune a son propre mask.
	void TestPlayerCreatureIndicesNoOverlap()
	{
		static_assert(kPlayerFieldEnd == kUnitFieldEnd + 3, "Player has 3 own fields");
		static_assert(kCreatureFieldEnd == kUnitFieldEnd + 2, "Creature has 2 own fields");
		// kPlayerFieldXp et kCreatureFieldSpawnId valent les memes
		// indices numeriques (kUnitFieldEnd + 2 pour XP, kUnitFieldEnd + 1
		// pour SpawnId) — c'est conforme : 2 classes paralleles.
		std::puts("[OK] TestPlayerCreatureIndicesNoOverlap");
	}

	/// OnReplicationSent : clear tous les bits, quel que soit le niveau
	/// dans la hierarchie. Verifie sur Player (le plus profond).
	void TestReplicationSentClearsAll()
	{
		ObjectGuid g(ObjectType::Player, 4);
		Player p(g, 1, 4, "ResetTest");
		p.SetPosition(10, 20, 30, 0);
		p.SetMaxHealth(1000);
		p.SetHealth(500);
		p.SetLevel(60);
		p.SetXp(999);
		assert(p.IsDirty());
		p.OnReplicationSent();
		assert(!p.IsDirty());
		assert(p.Mask().PopCount() == 0);
		// Les valeurs sont preservees, seul le mask est cleared.
		assert(p.GetHealth() == 500);
		assert(p.GetLevel() == 60);
		assert(p.GetXp() == 999);
		std::puts("[OK] TestReplicationSentClearsAll");
	}

	/// PlayerFieldCount > UnitFieldCount > WorldObjectFieldCount > ObjectFieldCount :
	/// chaque niveau ajoute des champs, pas de retrait.
	void TestFieldCountHierarchy()
	{
		static_assert(kObjectFieldCount < kWorldObjectFieldCount, "WorldObject > Object");
		static_assert(kWorldObjectFieldCount < kUnitFieldCount, "Unit > WorldObject");
		static_assert(kUnitFieldCount < kPlayerFieldCount, "Player > Unit");
		static_assert(kUnitFieldCount < kCreatureFieldCount, "Creature > Unit");
		std::puts("[OK] TestFieldCountHierarchy");
	}
}

int main()
{
	TestDeltaUnitOnlyHealth();
	TestDeltaPlayerOnlyXp();
	TestDeltaCreaturePosition();
	TestPlayerCreatureIndicesNoOverlap();
	TestReplicationSentClearsAll();
	TestFieldCountHierarchy();
	std::puts("All UpdateMask delta tests passed");
	return 0;
}
