/**
 * Unit tests for engine::core::util::UniqueTrackablePtr / TrackerRef.
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "engine/core/util/UniqueTrackablePtr.h"

#include <iostream>
#include <utility>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	struct Probe
	{
		int value;
		static int s_aliveCount;

		explicit Probe(int v) : value(v) { ++s_aliveCount; }
		~Probe() { --s_aliveCount; }
	};
	int Probe::s_aliveCount = 0;
}

using engine::core::util::TrackerRef;
using engine::core::util::UniqueTrackablePtr;

static void TestBasicOwnership()
{
	Probe::s_aliveCount = 0;
	{
		UniqueTrackablePtr<Probe> p(new Probe(7));
		Assert(p, "owner truthy");
		Assert(p.Get() != nullptr, "Get non-null");
		Assert(p->value == 7, "operator-> works");
		Assert((*p).value == 7, "operator* works");
		Assert(Probe::s_aliveCount == 1, "1 alive while owner exists");
	}
	Assert(Probe::s_aliveCount == 0, "destructor releases the object");
}

static void TestEmptyOwner()
{
	UniqueTrackablePtr<Probe> p;
	Assert(!p, "default owner falsy");
	Assert(p.Get() == nullptr, "Get null on empty");

	UniqueTrackablePtr<Probe> p2(nullptr);
	Assert(!p2, "owner from null falsy");
	Assert(!p2.Track(), "Track from empty owner is empty TrackerRef");
}

static void TestTrackerRefValidWhileOwnerAlive()
{
	UniqueTrackablePtr<Probe> p(new Probe(42));
	TrackerRef<Probe> ref = p.Track();
	Assert(ref, "ref truthy while owner alive");
	Assert(ref.Get() == p.Get(), "ref.Get == owner.Get");
	Assert(ref->value == 42, "ref operator-> works");
}

static void TestTrackerRefNullAfterOwnerDestroyed()
{
	Probe::s_aliveCount = 0;
	TrackerRef<Probe> ref;
	{
		UniqueTrackablePtr<Probe> p(new Probe(1));
		ref = p.Track();
		Assert(ref.Get() != nullptr, "ref valid in scope");
	}
	Assert(Probe::s_aliveCount == 0, "object destroyed when owner goes out of scope");
	Assert(ref.Get() == nullptr, "ref nullptr after owner destroyed");
	Assert(!ref, "ref falsy after owner destroyed");
}

static void TestMultipleTrackerRefs()
{
	Probe::s_aliveCount = 0;
	UniqueTrackablePtr<Probe> p(new Probe(99));
	TrackerRef<Probe> a = p.Track();
	TrackerRef<Probe> b = p.Track();
	TrackerRef<Probe> c = a; // copy
	Assert(a && b && c, "all three refs valid");
	Assert(a.Get() == b.Get() && b.Get() == c.Get(), "refs point to same object");

	p.Reset();
	Assert(Probe::s_aliveCount == 0, "Reset destroys object");
	Assert(!a && !b && !c, "all refs invalid after Reset");
}

static void TestMoveSemantics()
{
	Probe::s_aliveCount = 0;
	UniqueTrackablePtr<Probe> p1(new Probe(5));
	TrackerRef<Probe> ref = p1.Track();
	Assert(ref, "ref valid before move");

	UniqueTrackablePtr<Probe> p2(std::move(p1));
	Assert(!p1, "moved-from owner empty");
	Assert(p2 && p2->value == 5, "moved-to owner has the object");
	Assert(ref, "ref still valid after move (object not destroyed)");

	UniqueTrackablePtr<Probe> p3;
	p3 = std::move(p2);
	Assert(!p2, "moved-from owner empty after move-assign");
	Assert(p3 && p3->value == 5, "p3 has the object");
	Assert(ref, "ref still valid after move-assign");

	p3.Reset();
	Assert(!ref, "ref invalid after final Reset");
	Assert(Probe::s_aliveCount == 0, "object destroyed exactly once");
}

static void TestReleaseDoesNotDestroyButInvalidates()
{
	Probe::s_aliveCount = 0;
	UniqueTrackablePtr<Probe> p(new Probe(11));
	TrackerRef<Probe> ref = p.Track();
	Probe* raw = p.Release();

	Assert(raw != nullptr && raw->value == 11, "Release returns the raw pointer");
	Assert(Probe::s_aliveCount == 1, "object NOT destroyed by Release");
	Assert(!ref, "TrackerRef invalidated by Release");
	Assert(!p, "owner empty after Release");

	delete raw; // caller is responsible
	Assert(Probe::s_aliveCount == 0, "manual delete after Release");
}

static void TestSpellTargetUseCase()
{
	// Cas réaliste : un Spell garde un TrackerRef sur sa cible. Si la cible
	// meurt avant la résolution, le sort doit observer nullptr.
	struct Unit { int hp = 100; };
	struct Spell {
		TrackerRef<Unit> target;
		bool ResolveDamage(int dmg) {
			Unit* t = target.Get();
			if (!t) return false; // cible morte, sort fizzle
			t->hp -= dmg;
			return true;
		}
	};

	Spell spell;
	{
		UniqueTrackablePtr<Unit> caster(new Unit{});
		spell.target = caster.Track();
		Assert(spell.ResolveDamage(10), "damage applied while target alive");
		Assert(caster->hp == 90, "hp reduced");
	}
	// caster détruit → spell.target devient invalide.
	Assert(!spell.ResolveDamage(50), "damage fizzles after target died");
}

int main()
{
	TestBasicOwnership();
	TestEmptyOwner();
	TestTrackerRefValidWhileOwnerAlive();
	TestTrackerRefNullAfterOwnerDestroyed();
	TestMultipleTrackerRefs();
	TestMoveSemantics();
	TestReleaseDoesNotDestroyButInvalidates();
	TestSpellTargetUseCase();

	if (s_failCount > 0)
	{
		std::cerr << "[UniqueTrackablePtrTests] " << s_failCount << " failure(s)" << std::endl;
		return 1;
	}
	std::cout << "[UniqueTrackablePtrTests] OK" << std::endl;
	return 0;
}
