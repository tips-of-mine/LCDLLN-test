// Wave 17 entities tests : WorldObject (position, map, zone, IsInWorld).
// Pattern aligne sur ObjectGuidTests.cpp : asserts + printf, pas de framework.
// Cible CTest : worldobject_tests (cf. src/CMakeLists.txt).

#include "src/shardd/entities/WorldObject.h"

#include <cassert>
#include <cstdio>

using namespace engine::server::entities;

namespace
{
	/// Construction basique : guid preserve, position et flag inWorld par defaut.
	void TestWorldObjectConstruction()
	{
		ObjectGuid g(ObjectType::GameObject, 42);
		WorldObject obj(g, kWorldObjectFieldCount);
		assert(obj.Guid() == g);
		assert(obj.GetMapId() == 0);
		assert(obj.GetZoneId() == 0);
		assert(obj.GetPosX() == 0.0f);
		assert(obj.IsInWorld() == false);
		std::puts("[OK] TestWorldObjectConstruction");
	}

	/// SetPosition met les 4 champs (x/y/z/orientation) dirty en une fois.
	void TestWorldObjectPositionSet()
	{
		ObjectGuid g(ObjectType::GameObject, 1);
		WorldObject obj(g, kWorldObjectFieldCount);
		obj.SetPosition(100.5f, 200.5f, 30.0f, 1.57f);
		assert(obj.GetPosX() == 100.5f);
		assert(obj.GetPosY() == 200.5f);
		assert(obj.GetPosZ() == 30.0f);
		assert(obj.GetOrientation() == 1.57f);
		// 4 bits dirty : pos x/y/z + orientation
		assert(obj.Mask().PopCount() == 4);
		std::puts("[OK] TestWorldObjectPositionSet");
	}

	/// AddToWorld / RemoveFromWorld toggle le flag, sans toucher au mask.
	void TestWorldObjectAddRemoveWorld()
	{
		ObjectGuid g(ObjectType::Creature, 99);
		WorldObject obj(g, kWorldObjectFieldCount);
		assert(obj.IsInWorld() == false);
		obj.AddToWorld();
		assert(obj.IsInWorld() == true);
		obj.RemoveFromWorld();
		assert(obj.IsInWorld() == false);
		std::puts("[OK] TestWorldObjectAddRemoveWorld");
	}

	/// Set MapId et ZoneId : chacun marque son propre bit.
	void TestWorldObjectMapZone()
	{
		ObjectGuid g(ObjectType::Player, 7);
		WorldObject obj(g, kWorldObjectFieldCount);
		obj.SetMapId(1);
		obj.SetZoneId(42);
		assert(obj.GetMapId() == 1);
		assert(obj.GetZoneId() == 42);
		assert(obj.Mask().TestBit(kWorldObjectFieldMapId));
		assert(obj.Mask().TestBit(kWorldObjectFieldZoneId));
		std::puts("[OK] TestWorldObjectMapZone");
	}

	/// Set position avec meme valeur : pas de re-flag (UpdateField::Set
	/// teste l'egalite).
	void TestWorldObjectPositionIdempotent()
	{
		ObjectGuid g(ObjectType::Player, 8);
		WorldObject obj(g, kWorldObjectFieldCount);
		obj.SetPosition(1.0f, 2.0f, 3.0f, 0.5f);
		obj.OnReplicationSent();
		assert(!obj.IsDirty());
		// Re-set meme valeurs : aucun changement → pas dirty
		obj.SetPosition(1.0f, 2.0f, 3.0f, 0.5f);
		assert(!obj.IsDirty());
		std::puts("[OK] TestWorldObjectPositionIdempotent");
	}
}

int main()
{
	TestWorldObjectConstruction();
	TestWorldObjectPositionSet();
	TestWorldObjectAddRemoveWorld();
	TestWorldObjectMapZone();
	TestWorldObjectPositionIdempotent();
	std::puts("All WorldObject tests passed");
	return 0;
}
