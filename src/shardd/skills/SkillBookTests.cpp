#include "engine/server/skills/SkillBook.h"
#include "engine/core/Log.h"

namespace
{
	using namespace engine::server::skills;

	bool TestSetClampedToCap()
	{
		SkillBook b;
		b.Set(1, 250, 150); // demande 250 mais cap a 150
		auto* e = b.Get(1);
		if (!e || e->value != 150 || e->cap != 150) return false;
		LOG_INFO(Core, "[SkillTests] set clamped OK");
		return true;
	}

	bool TestGain()
	{
		SkillBook b;
		b.Set(2, 100, 200);
		// gain 50 -> 150
		if (b.Gain(2, 50) != 50) return false;
		if (b.Get(2)->value != 150) return false;
		// gain 100 -> clampe a 200, applique 50
		if (b.Gain(2, 100) != 50) return false;
		if (b.Get(2)->value != 200) return false;
		// gain sur skill inconnu = 0
		if (b.Gain(99, 10) != 0) return false;
		LOG_INFO(Core, "[SkillTests] gain OK");
		return true;
	}

	bool TestEffectiveWithBonus()
	{
		SkillBook b;
		b.Set(3, 100, 300);
		b.SetBonus(3, 25);
		if (b.Effective(3) != 125) return false;
		// skill inconnu : 0
		if (b.Effective(99) != 0) return false;
		LOG_INFO(Core, "[SkillTests] effective with bonus OK");
		return true;
	}

	bool TestEffectiveOverflowSafe()
	{
		SkillBook b;
		b.Set(4, 60000, 65000);
		b.SetBonus(4, 60000);
		// 60000 + 60000 = 120000 > 65535, clampe a 65535
		if (b.Effective(4) != 0xFFFFu) return false;
		LOG_INFO(Core, "[SkillTests] effective overflow safe OK");
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

	const bool ok = TestSetClampedToCap() && TestGain() && TestEffectiveWithBonus()
	             && TestEffectiveOverflowSafe();
	if (ok) LOG_INFO(Core, "[SkillTests] ALL OK");
	else LOG_ERROR(Core, "[SkillTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
