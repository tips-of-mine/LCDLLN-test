#include "engine/server/formulas/Formulas.h"
#include "engine/core/Log.h"

namespace
{
	using namespace engine::server::formulas;

	bool TestXpProgression()
	{
		if (XpToNextLevel(0)  != 0) return false;
		if (XpToNextLevel(80) != 0) return false;
		if (XpToNextLevel(100)!= 0) return false;
		// progression : level 1 < level 2 < level 79
		if (!(XpToNextLevel(1) < XpToNextLevel(2))) return false;
		if (!(XpToNextLevel(2) < XpToNextLevel(79))) return false;
		LOG_INFO(Core, "[FormulasTests] xp progression OK");
		return true;
	}

	bool TestAggroRadius()
	{
		if (AggroRadiusYards(0)  != 20.0f) return false;
		if (AggroRadiusYards(5)  != 25.0f) return false;
		if (AggroRadiusYards(-5) != 15.0f) return false;
		// clamp
		if (AggroRadiusYards(100) != 45.0f) return false;
		if (AggroRadiusYards(-100)!= 5.0f)  return false;
		LOG_INFO(Core, "[FormulasTests] aggro radius OK");
		return true;
	}

	bool TestDropMultiplier()
	{
		if (DropQuantityMultiplier(1, false) != 1.0f) return false;
		if (DropQuantityMultiplier(1, true)  != 1.5f) return false;
		// 5 joueurs + elite : 1.0 * 1.5 * 1.1 = 1.65
		float m = DropQuantityMultiplier(5, true);
		if (m < 1.64f || m > 1.66f) return false;
		LOG_INFO(Core, "[FormulasTests] drop multiplier OK");
		return true;
	}

	bool TestWarriorHp()
	{
		if (WarriorBaseHealth(0)  != 0) return false;
		if (WarriorBaseHealth(1)  != 150) return false;
		if (WarriorBaseHealth(60) != 100u + 50u*60u) return false;
		// au niveau 70 : 100 + 50*70 + 5*10 = 3650
		if (WarriorBaseHealth(70) != 3650) return false;
		LOG_INFO(Core, "[FormulasTests] warrior HP OK");
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

	const bool ok = TestXpProgression() && TestAggroRadius() && TestDropMultiplier()
	             && TestWarriorHp();
	if (ok) LOG_INFO(Core, "[FormulasTests] ALL OK");
	else LOG_ERROR(Core, "[FormulasTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
