#include "src/shared/formulas/Formulas.h"
#include "src/shared/core/Log.h"

namespace
{
	using namespace engine::server::formulas;

	bool TestXpProgression()
	{
		// Params du design (character_stats.json) passés explicitement.
		constexpr double kBase = 6.185;
		constexpr double kFactor = 2.6;
		constexpr uint8_t kMax = 100;

		// Cap : 0 hors plage et au niveau max.
		if (XpToNextLevel(0,   kBase, kFactor, kMax) != 0) return false;
		if (XpToNextLevel(100, kBase, kFactor, kMax) != 0) return false;
		// Positivité sur 1..99.
		if (XpToNextLevel(1,  kBase, kFactor, kMax) == 0) return false;
		if (XpToNextLevel(99, kBase, kFactor, kMax) == 0) return false;
		// Monotonie stricte.
		if (!(XpToNextLevel(1, kBase, kFactor, kMax) < XpToNextLevel(2, kBase, kFactor, kMax))) return false;
		if (!(XpToNextLevel(2, kBase, kFactor, kMax) < XpToNextLevel(99, kBase, kFactor, kMax))) return false;
		// Ancrage forme : XP(10) ~= round(6.185 * 10^2.6) = round(6.185 * 398.107) = 2463.
		const uint32_t x10 = XpToNextLevel(10, kBase, kFactor, kMax);
		if (x10 < 2460u || x10 > 2466u) return false;
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
