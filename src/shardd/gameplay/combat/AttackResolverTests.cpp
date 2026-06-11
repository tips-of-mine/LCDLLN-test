// Combat SP2 — tests du résolveur d'attaque pur (précision / critique / clamps).

#include "src/shardd/gameplay/combat/AttackResolver.h"
#include "src/shared/core/Log.h"

namespace
{
	using engine::server::combat::AttackRollResult;
	using engine::server::combat::ResolveAttackRoll;

	bool TestGuaranteedHit()
	{
		// accuracy 100 → ne rate jamais, même avec le pire jet (0.999...).
		const AttackRollResult r = ResolveAttackRoll(50, 100.0f, 0.0f, 1.5f, 0.999f, 0.5f);
		if (r.miss || r.crit || r.damage != 50) return false;
		LOG_INFO(Core, "[AttackResolverTests] guaranteed hit OK");
		return true;
	}

	bool TestGuaranteedMiss()
	{
		// accuracy 0 → rate toujours, même avec le meilleur jet (0.0).
		const AttackRollResult r = ResolveAttackRoll(50, 0.0f, 100.0f, 2.0f, 0.0f, 0.0f);
		if (!r.miss || r.crit || r.damage != 0) return false;
		LOG_INFO(Core, "[AttackResolverTests] guaranteed miss OK");
		return true;
	}

	bool TestGuaranteedCrit()
	{
		// critRate 100 → tout coup touché est critique ; 50 × 1.5 = 75.
		const AttackRollResult r = ResolveAttackRoll(50, 100.0f, 100.0f, 1.5f, 0.0f, 0.999f);
		if (r.miss || !r.crit || r.damage != 75) return false;
		LOG_INFO(Core, "[AttackResolverTests] guaranteed crit OK");
		return true;
	}

	bool TestNoCritWhenRateZero()
	{
		const AttackRollResult r = ResolveAttackRoll(50, 100.0f, 0.0f, 2.0f, 0.0f, 0.0f);
		if (r.miss || r.crit || r.damage != 50) return false;
		LOG_INFO(Core, "[AttackResolverTests] no crit at rate 0 OK");
		return true;
	}

	bool TestAccuracyClampedAbove100()
	{
		// accuracy 150 clampée à 100 → jamais de raté.
		const AttackRollResult r = ResolveAttackRoll(10, 150.0f, 0.0f, 1.5f, 0.999f, 0.5f);
		if (r.miss || r.damage != 10) return false;
		LOG_INFO(Core, "[AttackResolverTests] accuracy clamp OK");
		return true;
	}

	bool TestCritMultFlooredToOne()
	{
		// critMult 0.5 plancher 1.0 → un crit ne réduit jamais les dégâts.
		const AttackRollResult r = ResolveAttackRoll(40, 100.0f, 100.0f, 0.5f, 0.0f, 0.0f);
		if (r.miss || !r.crit || r.damage != 40) return false;
		LOG_INFO(Core, "[AttackResolverTests] critMult floor OK");
		return true;
	}

	bool TestZeroBaseDamage()
	{
		// base 0 → 0 dégât même en critique (pas d'underflow / arrondi exotique).
		const AttackRollResult r = ResolveAttackRoll(0, 100.0f, 100.0f, 3.0f, 0.0f, 0.0f);
		if (r.miss || !r.crit || r.damage != 0) return false;
		LOG_INFO(Core, "[AttackResolverTests] zero base damage OK");
		return true;
	}

	bool TestCritRounding()
	{
		// 7 × 1.5 = 10.5 → arrondi au plus proche = 11 (llround).
		const AttackRollResult r = ResolveAttackRoll(7, 100.0f, 100.0f, 1.5f, 0.0f, 0.0f);
		if (r.damage != 11) return false;
		LOG_INFO(Core, "[AttackResolverTests] crit rounding OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	(void)argc; (void)argv;
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	const bool ok = TestGuaranteedHit()
		&& TestGuaranteedMiss()
		&& TestGuaranteedCrit()
		&& TestNoCritWhenRateZero()
		&& TestAccuracyClampedAbove100()
		&& TestCritMultFlooredToOne()
		&& TestZeroBaseDamage()
		&& TestCritRounding();

	if (ok) LOG_INFO(Core, "[AttackResolverTests] ALL OK");
	else LOG_ERROR(Core, "[AttackResolverTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
