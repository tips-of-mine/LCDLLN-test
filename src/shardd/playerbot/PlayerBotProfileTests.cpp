#include "src/shardd/playerbot/PlayerBotProfile.h"
#include "src/shared/core/Log.h"

namespace
{
	using namespace engine::server::playerbot;

	bool TestEmpty()
	{
		PlayerBotScheduler s;
		auto picks = s.Pick(1, 5, {});
		if (!picks.empty()) return false;
		LOG_INFO(Core, "[PlayerBotTests] empty OK");
		return true;
	}

	bool TestPickByZone()
	{
		PlayerBotScheduler s;
		s.RegisterBot({1, "Aerith", BotClass::Mage,    BotRole::RangedDps, 60, 1, 100});
		s.RegisterBot({2, "Bork",   BotClass::Warrior, BotRole::Tank,      60, 1, 100});
		s.RegisterBot({3, "Cael",   BotClass::Druid,   BotRole::Heal,      60, 1, 200});

		auto picks = s.Pick(100, 10, {});
		if (picks.size() != 2) return false; // seulement 2 dans zone 100
		LOG_INFO(Core, "[PlayerBotTests] pick by zone OK");
		return true;
	}

	bool TestExcludeSpawned()
	{
		PlayerBotScheduler s;
		s.RegisterBot({1, "A", BotClass::Mage,    BotRole::RangedDps, 60, 1, 100});
		s.RegisterBot({2, "B", BotClass::Warrior, BotRole::Tank,      60, 1, 100});
		s.RegisterBot({3, "C", BotClass::Priest,  BotRole::Heal,      60, 1, 100});

		auto picks = s.Pick(100, 10, {1, 2});
		if (picks.size() != 1) return false;
		if (picks[0] != 3) return false;
		LOG_INFO(Core, "[PlayerBotTests] exclude spawned OK");
		return true;
	}

	bool TestNeededLimit()
	{
		PlayerBotScheduler s;
		for (BotId i = 1; i <= 5; ++i)
			s.RegisterBot({i, "Bot", BotClass::Warrior, BotRole::MeleeDps, 60, 1, 100});

		auto picks = s.Pick(100, 2, {});
		if (picks.size() != 2) return false;
		LOG_INFO(Core, "[PlayerBotTests] needed limit OK");
		return true;
	}

	bool TestGet()
	{
		PlayerBotScheduler s;
		s.RegisterBot({42, "Hero", BotClass::Paladin, BotRole::Tank, 70, 2, 300});
		auto* p = s.Get(42);
		if (!p) return false;
		if (p->level != 70 || p->aiTier != 2) return false;
		if (s.Get(99)) return false;
		LOG_INFO(Core, "[PlayerBotTests] Get OK");
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

	const bool ok = TestEmpty() && TestPickByZone() && TestExcludeSpawned()
	             && TestNeededLimit() && TestGet();
	if (ok) LOG_INFO(Core, "[PlayerBotTests] ALL OK");
	else LOG_ERROR(Core, "[PlayerBotTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
