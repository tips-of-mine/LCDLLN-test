// Wave 7 foundation tests : ObjectGuid encoding + UpdateMask bit ops +
// UpdateField auto-flag + Object base class dirty tracking.
//
// Pattern aligne sur LunarCalendarTests.cpp : pas de framework, asserts +
// printf simples. Le binaire est enregistre dans CMakeLists.txt comme
// cible CTest `entities_foundation_tests`.

#include "src/shardd/entities/ObjectGuid.h"
#include "src/shardd/entities/UpdateMask.h"
#include "src/shardd/entities/UpdateField.h"
#include "src/shardd/entities/Object.h"

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>

using namespace engine::server::entities;

namespace
{
	/// Round-trip basique : type + id sortent identiques apres construction.
	void TestObjectGuidEncoding()
	{
		ObjectGuid g(ObjectType::Player, 1234);
		assert(g.Type() == ObjectType::Player);
		assert(g.Id() == 1234);
		assert(g.Raw() != 0);
		std::puts("[OK] TestObjectGuidEncoding");
	}

	/// Construction par defaut : guid vide, type None, id 0.
	void TestObjectGuidEmpty()
	{
		ObjectGuid g;
		assert(g.IsEmpty());
		assert(g.Type() == ObjectType::None);
		assert(g.Id() == 0);
		std::puts("[OK] TestObjectGuidEmpty");
	}

	/// Id maximal (56 bits tous a 1) : doit etre preserve sans clipping.
	void TestObjectGuidMaxId()
	{
		ObjectGuid g(ObjectType::Creature, ObjectGuid::kIdMask);
		assert(g.Type() == ObjectType::Creature);
		assert(g.Id() == ObjectGuid::kIdMask);
		std::puts("[OK] TestObjectGuidMaxId");
	}

	/// Id > 56 bits : doit etre tronque proprement sans contaminer le type.
	void TestObjectGuidIdMaskClipping()
	{
		uint64_t bigId = 0xFFFFFFFFFFFFFFFFull;
		ObjectGuid g(ObjectType::Player, bigId);
		assert(g.Id() == ObjectGuid::kIdMask);
		assert(g.Type() == ObjectType::Player);
		std::puts("[OK] TestObjectGuidIdMaskClipping");
	}

	/// Egalite : memes type+id sont egaux, type different inverse.
	void TestObjectGuidEquality()
	{
		ObjectGuid a(ObjectType::Player, 100);
		ObjectGuid b(ObjectType::Player, 100);
		ObjectGuid c(ObjectType::Creature, 100);
		assert(a == b);
		assert(a != c);
		std::puts("[OK] TestObjectGuidEquality");
	}

	/// Helper FormatGuid : libelle + id pour log/debug.
	void TestFormatGuid()
	{
		ObjectGuid g(ObjectType::Player, 42);
		assert(FormatGuid(g) == "Player#42");
		ObjectGuid empty;
		assert(FormatGuid(empty) == "Empty");
		std::puts("[OK] TestFormatGuid");
	}

	/// Helper ObjectTypeToString : couvre toutes les valeurs nommees + Unknown
	/// pour une valeur non listee.
	void TestObjectTypeToString()
	{
		assert(std::string(ObjectTypeToString(ObjectType::None)) == "None");
		assert(std::string(ObjectTypeToString(ObjectType::Player)) == "Player");
		assert(std::string(ObjectTypeToString(ObjectType::Creature)) == "Creature");
		assert(std::string(ObjectTypeToString(ObjectType::GameObject)) == "GameObject");
		assert(std::string(ObjectTypeToString(ObjectType::Item)) == "Item");
		assert(std::string(ObjectTypeToString(ObjectType::Container)) == "Container");
		assert(std::string(ObjectTypeToString(ObjectType::Corpse)) == "Corpse");
		assert(std::string(ObjectTypeToString(ObjectType::DynamicObject)) == "DynamicObject");
		assert(std::string(ObjectTypeToString(ObjectType::Pet)) == "Pet");
		assert(std::string(ObjectTypeToString(ObjectType::Vehicle)) == "Vehicle");
		// Valeur reservee non utilisee : retourne "Unknown".
		assert(std::string(ObjectTypeToString(static_cast<ObjectType>(200))) == "Unknown");
		std::puts("[OK] TestObjectTypeToString");
	}

	/// Operations de base : Set / Clear / Test / Empty / PopCount.
	void TestUpdateMaskBasic()
	{
		UpdateMask m(64);
		assert(m.FieldCount() == 64);
		assert(m.Empty());
		assert(m.PopCount() == 0);

		m.SetBit(5);
		m.SetBit(31);
		m.SetBit(63);
		assert(!m.Empty());
		assert(m.PopCount() == 3);
		assert(m.TestBit(5));
		assert(m.TestBit(31));
		assert(m.TestBit(63));
		assert(!m.TestBit(0));
		assert(!m.TestBit(32));

		m.ClearBit(5);
		assert(m.PopCount() == 2);
		assert(!m.TestBit(5));

		m.Clear();
		assert(m.Empty());
		std::puts("[OK] TestUpdateMaskBasic");
	}

	/// SetBit / TestBit hors plage : no-op silencieux.
	void TestUpdateMaskOutOfRange()
	{
		UpdateMask m(8);
		m.SetBit(8);   // hors plage (FieldCount=8, valides=0..7)
		m.SetBit(100);
		assert(m.Empty());
		assert(!m.TestBit(8));
		assert(!m.TestBit(100));
		std::puts("[OK] TestUpdateMaskOutOfRange");
	}

	/// Resize remet tout a 0, meme les indices precedemment valides.
	void TestUpdateMaskResize()
	{
		UpdateMask m(4);
		m.SetBit(0);
		m.SetBit(3);
		assert(m.PopCount() == 2);

		m.Resize(64);
		assert(m.FieldCount() == 64);
		assert(m.Empty());
		assert(m.PopCount() == 0);
		std::puts("[OK] TestUpdateMaskResize");
	}

	/// UpdateField : Set modifie + flag, Set meme valeur ne flag pas,
	/// MarkDirty force le flag.
	void TestUpdateField()
	{
		UpdateMask mask(4);
		UpdateField<int> field(2, &mask, 0);
		assert(field.Get() == 0);
		assert(mask.Empty());

		field.Set(42);
		assert(field.Get() == 42);
		assert(mask.TestBit(2));
		assert(mask.PopCount() == 1);

		// Set meme valeur : pas de re-flag.
		mask.Clear();
		field.Set(42);
		assert(mask.Empty());

		// MarkDirty force.
		field.MarkDirty();
		assert(mask.TestBit(2));
		std::puts("[OK] TestUpdateField");
	}

	/// UpdateField detache (mask=nullptr) : Set / MarkDirty ne crashent pas.
	void TestUpdateFieldDetached()
	{
		UpdateField<int> field(0, nullptr, 10);
		assert(field.Get() == 10);
		field.Set(20);
		assert(field.Get() == 20);
		field.MarkDirty(); // no-op silencieux, pas de crash
		std::puts("[OK] TestUpdateFieldDetached");
	}

	/// Object : guid immuable, Mask() mutable, IsDirty reflete le mask,
	/// OnReplicationSent reset.
	void TestObjectBase()
	{
		ObjectGuid guid(ObjectType::Player, 12345);
		Object obj(guid, 16);
		assert(obj.Guid() == guid);
		assert(obj.Type() == ObjectType::Player);
		assert(!obj.IsDirty());

		obj.Mask().SetBit(3);
		assert(obj.IsDirty());

		obj.OnReplicationSent();
		assert(!obj.IsDirty());
		std::puts("[OK] TestObjectBase");
	}

	/// Le hash std::hash<ObjectGuid> est deterministe sur Raw().
	void TestObjectGuidHashable()
	{
		std::hash<ObjectGuid> hasher;
		ObjectGuid a(ObjectType::Player, 1);
		ObjectGuid b(ObjectType::Player, 1);
		assert(hasher(a) == hasher(b));
		// hash(a) != hash(c) avec haute probabilite (pas un test strict).
		std::puts("[OK] TestObjectGuidHashable");
	}

	/// Round-trip Raw -> ObjectGuid -> Raw conserve la valeur exacte.
	void TestObjectGuidRawRoundTrip()
	{
		uint64_t expected = (static_cast<uint64_t>(ObjectType::Creature) << 56) | 0x123456789ABCull;
		ObjectGuid g(expected);
		assert(g.Raw() == expected);
		assert(g.Type() == ObjectType::Creature);
		assert(g.Id() == 0x123456789ABCull);

		ObjectGuid g2(g.Type(), g.Id());
		assert(g2.Raw() == g.Raw());
		std::puts("[OK] TestObjectGuidRawRoundTrip");
	}
}

int main()
{
	TestObjectGuidEncoding();
	TestObjectGuidEmpty();
	TestObjectGuidMaxId();
	TestObjectGuidIdMaskClipping();
	TestObjectGuidEquality();
	TestFormatGuid();
	TestObjectTypeToString();
	TestUpdateMaskBasic();
	TestUpdateMaskOutOfRange();
	TestUpdateMaskResize();
	TestUpdateField();
	TestUpdateFieldDetached();
	TestObjectBase();
	TestObjectGuidHashable();
	TestObjectGuidRawRoundTrip();
	std::puts("[ALL OK] EntitiesFoundationTests");
	return 0;
}
