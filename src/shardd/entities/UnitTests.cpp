// Wave 17 entities tests : Unit (HP/MP/level/faction, clamp, IsAlive,
// OnReplicationSent). Pattern aligne sur ObjectGuidTests.cpp : asserts +
// printf, pas de framework.
// Cible CTest : unit_tests (cf. src/CMakeLists.txt).

#include "src/shardd/entities/Unit.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace engine::server::entities;

namespace
{
	/// Construction basique : tous stats a zero, IsAlive=false (HP=0).
	void TestUnitConstruction()
	{
		ObjectGuid g(ObjectType::Creature, 1);
		Unit u(g, kUnitFieldCount);
		assert(u.GetHealth() == 0);
		assert(u.GetMaxHealth() == 0);
		assert(u.GetLevel() == 0);
		assert(u.GetFaction() == 0);
		assert(u.IsAlive() == false); // HP == 0 → dead
		std::puts("[OK] TestUnitConstruction");
	}

	/// SetHealth/SetMaxHealth : marque 2 bits dirty, IsAlive devient true.
	void TestUnitHealthSet()
	{
		ObjectGuid g(ObjectType::Creature, 2);
		Unit u(g, kUnitFieldCount);
		u.SetMaxHealth(1000);
		u.SetHealth(500);
		assert(u.GetHealth() == 500);
		assert(u.GetMaxHealth() == 1000);
		assert(u.IsAlive() == true);
		assert(u.Mask().TestBit(kUnitFieldHealth));
		assert(u.Mask().TestBit(kUnitFieldMaxHealth));
		std::puts("[OK] TestUnitHealthSet");
	}

	/// SetHealth > maxHealth : clamp a max. La valeur stockee = max.
	void TestUnitHealthClampOverflow()
	{
		ObjectGuid g(ObjectType::Creature, 3);
		Unit u(g, kUnitFieldCount);
		u.SetMaxHealth(100);
		u.SetHealth(200); // > max → clamp a max
		assert(u.GetHealth() == 100);
		std::puts("[OK] TestUnitHealthClampOverflow");
	}

	/// SetMana : meme logique de clamp que SetHealth.
	void TestUnitManaClamp()
	{
		ObjectGuid g(ObjectType::Creature, 4);
		Unit u(g, kUnitFieldCount);
		u.SetMaxMana(500);
		u.SetMana(1000); // > max → clamp a 500
		assert(u.GetMana() == 500);
		std::puts("[OK] TestUnitManaClamp");
	}

	/// Set Level + Faction : champs independants, chacun son bit.
	void TestUnitFactionLevel()
	{
		ObjectGuid g(ObjectType::Creature, 5);
		Unit u(g, kUnitFieldCount);
		u.SetLevel(60);
		u.SetFaction(35); // exemple : faction alliance
		assert(u.GetLevel() == 60);
		assert(u.GetFaction() == 35);
		assert(u.Mask().TestBit(kUnitFieldLevel));
		assert(u.Mask().TestBit(kUnitFieldFaction));
		std::puts("[OK] TestUnitFactionLevel");
	}

	/// OnReplicationSent : Mask().Clear(), IsDirty() devient false. La
	/// valeur des champs reste intacte (seul le mask est reset).
	void TestUnitOnReplicationSentClearsMask()
	{
		ObjectGuid g(ObjectType::Creature, 6);
		Unit u(g, kUnitFieldCount);
		u.SetMaxHealth(100);
		u.SetHealth(75);
		assert(u.IsDirty());
		u.OnReplicationSent();
		assert(!u.IsDirty());
		// Les valeurs sont preservees.
		assert(u.GetHealth() == 75);
		assert(u.GetMaxHealth() == 100);
		std::puts("[OK] TestUnitOnReplicationSentClearsMask");
	}

	/// IsAlive : false si HP = 0, true sinon. Toggle par SetHealth.
	void TestUnitIsAliveToggle()
	{
		ObjectGuid g(ObjectType::Creature, 7);
		Unit u(g, kUnitFieldCount);
		u.SetMaxHealth(100);
		u.SetHealth(100);
		assert(u.IsAlive());
		u.SetHealth(0);
		assert(!u.IsAlive());
		u.SetHealth(1);
		assert(u.IsAlive());
		std::puts("[OK] TestUnitIsAliveToggle");
	}

	/// Unit herite de WorldObject : la position fonctionne.
	void TestUnitHeriteWorldObject()
	{
		ObjectGuid g(ObjectType::Creature, 8);
		Unit u(g, kUnitFieldCount);
		u.SetPosition(10.0f, 20.0f, 30.0f, 0.5f);
		assert(u.GetPosX() == 10.0f);
		assert(u.GetMapId() == 0); // pas encore set
		u.SetMapId(1);
		assert(u.GetMapId() == 1);
		std::puts("[OK] TestUnitHeriteWorldObject");
	}

	/// Stats étendues (Système de Personnages) : round-trip des nouveaux
	/// UpdateField (entiers + flottants) et flag dirty via le mask.
	void TestUnitNewStatFields()
	{
#define CHECK_UNIT(cond) do { if (!(cond)) { std::fprintf(stderr, "[FAIL] UnitTests: %s\n", #cond); std::abort(); } } while (0)
		ObjectGuid g(ObjectType::Creature, 9);
		Unit u(g, kUnitFieldCount);
		u.SetDamage(123u);
		u.SetAccuracy(88.0f);
		u.SetRange(30.0f);
		u.SetCritRate(7.5f);
		u.SetCritMult(1.8f);
		u.SetSpeedWalk(2.0f);
		u.SetSpeedRun(5.0f);
		u.SetSpeedSprint(8.0f);
		u.SetStamina(500u);
		u.SetMaxStamina(800u);
		u.SetPerception(12.5f);
		u.SetStealth(9.0f);
		u.SetSecondaryResource(40u);
		u.SetMaxSecondaryResource(100u);

		// Round-trip valeurs (entiers exacts, flottants avec tolerance).
		CHECK_UNIT(u.GetDamage() == 123u);
		CHECK_UNIT(u.GetCritRate() >= 7.49f && u.GetCritRate() <= 7.51f);
		CHECK_UNIT(u.GetCritMult() >= 1.79f && u.GetCritMult() <= 1.81f);
		CHECK_UNIT(u.GetMaxStamina() == 800u);
		CHECK_UNIT(u.GetSecondaryResource() == 40u);
		CHECK_UNIT(u.GetMaxSecondaryResource() == 100u);
		CHECK_UNIT(u.GetPerception() >= 12.49f && u.GetPerception() <= 12.51f);

		// Le mask reflete les champs modifies.
		CHECK_UNIT(u.IsDirty());
		CHECK_UNIT(u.Mask().TestBit(kUnitFieldDamage));
		CHECK_UNIT(u.Mask().TestBit(kUnitFieldMaxSecondaryResource));
		std::puts("[OK] TestUnitNewStatFields");
#undef CHECK_UNIT
	}
}

int main()
{
	TestUnitConstruction();
	TestUnitHealthSet();
	TestUnitHealthClampOverflow();
	TestUnitManaClamp();
	TestUnitFactionLevel();
	TestUnitOnReplicationSentClearsMask();
	TestUnitIsAliveToggle();
	TestUnitHeriteWorldObject();
	TestUnitNewStatFields();
	std::puts("All Unit tests passed");
	return 0;
}
