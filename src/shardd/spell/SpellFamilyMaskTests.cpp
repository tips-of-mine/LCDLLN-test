#include "engine/server/spell/SpellFamilyMask.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::spell::SpellFamily;
	using engine::server::spell::SpellFamilyMask;
	using engine::server::spell::SpellInfo;
	using engine::server::spell::SpellMatchesProcCriteria;

	bool TestEmpty()
	{
		SpellFamilyMask m;
		if (!m.IsEmpty()) return false;
		SpellFamilyMask m2(1);
		if (m2.IsEmpty()) return false;
		LOG_INFO(Core, "[SpellFamilyMaskTests] empty OK");
		return true;
	}

	bool TestIntersect()
	{
		SpellFamilyMask a(0xFF00, 0, 0, 0);
		SpellFamilyMask b(0x0F00, 0, 0, 0);
		SpellFamilyMask c(0x0001, 0, 0, 0);
		if (!a.IntersectsWith(b)) return false;  // 0xFF00 & 0x0F00 = 0x0F00 ≠ 0
		if (a.IntersectsWith(c)) return false;    // 0xFF00 & 0x0001 = 0
		LOG_INFO(Core, "[SpellFamilyMaskTests] Intersects OK");
		return true;
	}

	bool TestContains()
	{
		SpellFamilyMask superset(0xFFFF, 0xFFFF, 0, 0);
		SpellFamilyMask subset(0x00FF, 0x0F00, 0, 0);
		SpellFamilyMask notSubset(0x10000, 0, 0, 0);
		if (!superset.Contains(subset)) return false;
		if (superset.Contains(notSubset)) return false;
		LOG_INFO(Core, "[SpellFamilyMaskTests] Contains OK");
		return true;
	}

	bool TestBitwiseOps()
	{
		SpellFamilyMask a(0xF0, 0, 0, 0);
		SpellFamilyMask b(0x0F, 0, 0, 0);
		auto orResult = a | b;
		if (orResult.parts[0] != 0xFF) return false;
		auto andResult = a & b;
		if (andResult.parts[0] != 0) return false;
		LOG_INFO(Core, "[SpellFamilyMaskTests] bitwise ops OK");
		return true;
	}

	bool TestProcMatching()
	{
		// Triggering spell : Mage, mask 0x10 (e.g. Fireball).
		SpellInfo trigger;
		trigger.spellId = 100;
		trigger.family = SpellFamily::Mage;
		trigger.familyMask = SpellFamilyMask(0x10);

		// ProcEvent : "any Mage spell" (anyOfMask vide).
		if (!SpellMatchesProcCriteria(trigger, SpellFamily::Mage, SpellFamilyMask()))
			return false;

		// ProcEvent : "Mage spells with mask 0x10" (Fire).
		if (!SpellMatchesProcCriteria(trigger, SpellFamily::Mage, SpellFamilyMask(0x10)))
			return false;

		// ProcEvent : "Mage spells with mask 0x20" (Frost) → no match.
		if (SpellMatchesProcCriteria(trigger, SpellFamily::Mage, SpellFamilyMask(0x20)))
			return false;

		// ProcEvent : "Warrior" → no match (wrong family).
		if (SpellMatchesProcCriteria(trigger, SpellFamily::Warrior, SpellFamilyMask()))
			return false;

		LOG_INFO(Core, "[SpellFamilyMaskTests] Proc matching OK");
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

	const bool ok = TestEmpty() && TestIntersect() && TestContains()
		&& TestBitwiseOps() && TestProcMatching();

	if (ok) LOG_INFO(Core, "[SpellFamilyMaskTests] ALL OK");
	else LOG_ERROR(Core, "[SpellFamilyMaskTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
