// Wave 23 — SpellMgr + Spell state machine + Aura tick + ProcMgr tests.
// Pattern aligne sur les autres tests Wave : asserts + printf.
// Cible CTest : spell_mgr_tests (consolide les 4 sous-domaines en 1 binaire).

#include "src/shardd/spell/SpellMgr.h"
#include "src/shardd/spell/Spell.h"
#include "src/shardd/spell/Aura.h"
#include "src/shardd/spell/ProcMgr.h"

#include <cassert>
#include <cstdio>
#include <random>
#include <string>

using namespace engine::server::spell;

namespace
{
	SpellTemplate MakeBasicSpell(SpellId id, uint32_t castMs, uint32_t durMs = 0,
		uint32_t tickMs = 0, int32_t basePts = 100)
	{
		SpellTemplate t;
		t.spellId      = id;
		t.name         = "TestSpell" + std::to_string(id);
		t.targetType   = SpellTargetType::SingleEnemy;
		t.castTimeMs   = castMs;
		t.durationMs   = durMs;
		t.tickPeriodMs = tickMs;
		t.basePoints   = basePts;
		return t;
	}

	// ========================================================================
	// SpellMgr
	// ========================================================================

	void TestSpellMgrRegisterFind()
	{
		SpellMgr mgr;
		mgr.Register(MakeBasicSpell(100, 1500));
		mgr.Register(MakeBasicSpell(200, 0));
		assert(mgr.TemplateCount() == 2);
		const auto* t = mgr.Find(100);
		assert(t != nullptr && t->castTimeMs == 1500);
		assert(mgr.Find(999) == nullptr);
		std::puts("[OK] TestSpellMgrRegisterFind");
	}

	void TestSpellMgrReregisterOverwrites()
	{
		SpellMgr mgr;
		mgr.Register(MakeBasicSpell(100, 1500));
		mgr.Register(MakeBasicSpell(100, 3000));  // overwrite
		assert(mgr.TemplateCount() == 1);
		assert(mgr.Find(100)->castTimeMs == 3000);
		std::puts("[OK] TestSpellMgrReregisterOverwrites");
	}

	// ========================================================================
	// Spell state machine
	// ========================================================================

	void TestSpellStateInstant()
	{
		auto tpl = MakeBasicSpell(1, 0);  // cast instant
		Spell s(tpl, /*target*/42);
		assert(s.State() == SpellState::Idle);
		assert(s.Begin());
		// castTimeMs = 0 -> Tick(0) doit faire passer Casting -> Casted
		s.Tick(0);
		assert(s.State() == SpellState::Casted);
		assert(s.Apply());
		assert(s.State() == SpellState::Resolved);
		assert(s.IsTerminal());
		std::puts("[OK] TestSpellStateInstant");
	}

	void TestSpellStateCastTime()
	{
		auto tpl = MakeBasicSpell(1, 1500);
		Spell s(tpl, 42);
		assert(s.Begin());
		s.Tick(500);
		assert(s.State() == SpellState::Casting);
		s.Tick(500);
		assert(s.State() == SpellState::Casting);
		s.Tick(500);
		assert(s.State() == SpellState::Casted);  // 1500ms elapsed
		assert(s.Apply());
		assert(s.State() == SpellState::Resolved);
		std::puts("[OK] TestSpellStateCastTime");
	}

	void TestSpellInterrupt()
	{
		auto tpl = MakeBasicSpell(1, 2000);
		Spell s(tpl, 42);
		assert(s.Begin());
		s.Tick(500);
		assert(s.Interrupt());
		assert(s.State() == SpellState::Interrupted);
		assert(s.IsTerminal());
		// Tick post-interrupt = no-op.
		s.Tick(1000);
		assert(s.State() == SpellState::Interrupted);
		// Apply post-interrupt = false.
		assert(!s.Apply());
		std::puts("[OK] TestSpellInterrupt");
	}

	void TestSpellInvalidTransitions()
	{
		auto tpl = MakeBasicSpell(1, 1000);
		Spell s(tpl, 42);
		// Apply avant Begin : false.
		assert(!s.Apply());
		// Interrupt avant Begin : false (pas Casting).
		assert(!s.Interrupt());
		// Begin 2x : 2e false.
		assert(s.Begin());
		assert(!s.Begin());
		std::puts("[OK] TestSpellInvalidTransitions");
	}

	// ========================================================================
	// Aura tick + stack
	// ========================================================================

	void TestAuraExpiration()
	{
		auto tpl = MakeBasicSpell(1, /*cast*/0, /*duration*/5000);
		Aura a(tpl, /*caster*/10, /*target*/20, /*nowMs*/1000);
		assert(!a.IsExpired(1000));
		assert(!a.IsExpired(5000));
		assert(!a.IsExpired(5999));
		assert(a.IsExpired(6000));   // applied=1000 + duration=5000 = 6000
		assert(a.IsExpired(10000));
		std::puts("[OK] TestAuraExpiration");
	}

	void TestAuraTickPeriodic()
	{
		// Duration 10s, tick toutes les 2s -> 4 ticks attendus (a 2,4,6,8s).
		// Le tick a 10s tombe pile sur expiresAtMs, donc le check < expires
		// l'exclut.
		auto tpl = MakeBasicSpell(1, 0, /*duration*/10000, /*tick*/2000);
		Aura a(tpl, 10, 20, /*applied*/0);
		// Avance jusqu'a 9000 ms -> ticks consommes a 2k,4k,6k,8k = 4 ticks.
		uint32_t total = a.AdvanceTick(9000);
		assert(total == 4);
		// Re-advance to 9500 : no new tick (next serait a 10000 = expires).
		total = a.AdvanceTick(9500);
		assert(total == 0);
		std::puts("[OK] TestAuraTickPeriodic");
	}

	void TestAuraStack()
	{
		auto tpl = MakeBasicSpell(1, 0, /*duration*/5000);
		Aura a1(tpl, /*caster*/10, /*target*/20, /*applied*/1000);
		Aura a2(tpl, /*caster*/10, /*target*/20, /*applied*/2000);
		assert(a1.CanStackWith(a2));
		// Stack : refresh + increment count.
		a1.Stack(2000);
		assert(a1.StackCount() == 2);
		assert(a1.ExpiresAtMs() == 7000);  // 2000 + 5000
		std::puts("[OK] TestAuraStack");
	}

	void TestAuraNoStackDifferentCaster()
	{
		auto tpl = MakeBasicSpell(1, 0, 5000);
		Aura a1(tpl, /*caster*/10, 20, 0);
		Aura a2(tpl, /*caster*/11, 20, 0);  // caster different
		assert(!a1.CanStackWith(a2));
		std::puts("[OK] TestAuraNoStackDifferentCaster");
	}

	void TestAuraNoTickIfPassive()
	{
		// Duration > 0 mais tickPeriodMs = 0 -> aura passive, pas de tick.
		auto tpl = MakeBasicSpell(1, 0, 5000, /*tick*/0);
		Aura a(tpl, 10, 20, 0);
		uint32_t ticks = a.AdvanceTick(10000);
		assert(ticks == 0);
		std::puts("[OK] TestAuraNoTickIfPassive");
	}

	// ========================================================================
	// ProcMgr
	// ========================================================================

	void TestProcMgrChanceFull()
	{
		ProcMgr proc;
		ProcTemplate t;
		t.procId = 1;
		t.event = ProcEvent::OnMeleeHit;
		t.triggerSpell = 999;
		t.procChance = 100;  // always
		proc.Register(t);

		std::mt19937 rng(42);
		std::unordered_map<uint32_t, uint64_t> cd;
		auto out = proc.OnEvent(ProcEvent::OnMeleeHit, rng, 1000, cd);
		assert(out.size() == 1 && out[0] == 999);
		std::puts("[OK] TestProcMgrChanceFull");
	}

	void TestProcMgrChanceZero()
	{
		ProcMgr proc;
		ProcTemplate t;
		t.procId = 2;
		t.event = ProcEvent::OnSpellCrit;
		t.triggerSpell = 555;
		t.procChance = 0;  // never
		proc.Register(t);

		std::mt19937 rng(42);
		std::unordered_map<uint32_t, uint64_t> cd;
		auto out = proc.OnEvent(ProcEvent::OnSpellCrit, rng, 1000, cd);
		assert(out.empty());
		std::puts("[OK] TestProcMgrChanceZero");
	}

	void TestProcMgrInternalCooldown()
	{
		ProcMgr proc;
		ProcTemplate t;
		t.procId = 3;
		t.event = ProcEvent::OnMeleeCrit;
		t.triggerSpell = 777;
		t.procChance = 100;  // always
		t.internalCooldownMs = 5000;
		proc.Register(t);

		std::mt19937 rng(42);
		std::unordered_map<uint32_t, uint64_t> cd;
		// Premier proc : OK.
		auto out1 = proc.OnEvent(ProcEvent::OnMeleeCrit, rng, 1000, cd);
		assert(out1.size() == 1);
		// 2e proc 2s plus tard : ICD bloque.
		auto out2 = proc.OnEvent(ProcEvent::OnMeleeCrit, rng, 3000, cd);
		assert(out2.empty());
		// 5.5s apres premier proc : OK.
		auto out3 = proc.OnEvent(ProcEvent::OnMeleeCrit, rng, 6500, cd);
		assert(out3.size() == 1);
		std::puts("[OK] TestProcMgrInternalCooldown");
	}

	void TestProcMgrEventMismatch()
	{
		ProcMgr proc;
		ProcTemplate t;
		t.procId = 4;
		t.event = ProcEvent::OnDeath;
		t.triggerSpell = 111;
		t.procChance = 100;
		proc.Register(t);

		std::mt19937 rng(42);
		std::unordered_map<uint32_t, uint64_t> cd;
		// Event different : aucun proc.
		auto out = proc.OnEvent(ProcEvent::OnMeleeHit, rng, 1000, cd);
		assert(out.empty());
		std::puts("[OK] TestProcMgrEventMismatch");
	}
}

int main()
{
	TestSpellMgrRegisterFind();
	TestSpellMgrReregisterOverwrites();
	TestSpellStateInstant();
	TestSpellStateCastTime();
	TestSpellInterrupt();
	TestSpellInvalidTransitions();
	TestAuraExpiration();
	TestAuraTickPeriodic();
	TestAuraStack();
	TestAuraNoStackDifferentCaster();
	TestAuraNoTickIfPassive();
	TestProcMgrChanceFull();
	TestProcMgrChanceZero();
	TestProcMgrInternalCooldown();
	TestProcMgrEventMismatch();
	std::puts("All SpellMgr/Spell/Aura/ProcMgr tests passed");
	return 0;
}
