#include "engine/server/arena/ArenaTeam.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::arena::ArenaTeam;
	using engine::server::arena::ArenaTeamRegistry;
	using engine::server::arena::TeamSize;
	using engine::server::arena::ApplyEloUpdate;

	bool TestEloEqual()
	{
		// Deux equipes a 1500. Gagnant +16, perdant -16 avec K=32.
		uint32_t w, l;
		ApplyEloUpdate(1500, 1500, 32, w, l);
		if (w != 1516 || l != 1484) return false;
		LOG_INFO(Core, "[ArenaTests] ELO equal OK");
		return true;
	}

	bool TestEloFavoriteWins()
	{
		// 1700 bat 1300 : gagne tres peu (favori attendu).
		uint32_t w, l;
		ApplyEloUpdate(1700, 1300, 32, w, l);
		// expected ~0.91, gain ~32*0.09 ~= 3
		if (!(w >= 1701 && w <= 1706)) return false;
		if (!(l >= 1294 && l <= 1299)) return false;
		LOG_INFO(Core, "[ArenaTests] ELO favorite wins OK");
		return true;
	}

	bool TestRecordMatch()
	{
		ArenaTeamRegistry r;
		ArenaTeam a{1, TeamSize::v2, "A", {}, 1500, 0, 0, 0, 0};
		ArenaTeam b{2, TeamSize::v2, "B", {}, 1500, 0, 0, 0, 0};
		r.AddTeam(a);
		r.AddTeam(b);
		if (!r.RecordMatch(1, 2)) return false;
		auto* aa = r.Get(1);
		auto* bb = r.Get(2);
		if (aa->rating != 1516) return false;
		if (bb->rating != 1484) return false;
		if (aa->seasonWins != 1 || aa->seasonGames != 1) return false;
		if (bb->seasonWins != 0 || bb->seasonGames != 1) return false;
		LOG_INFO(Core, "[ArenaTests] record match OK");
		return true;
	}

	bool TestUnknownMatch()
	{
		ArenaTeamRegistry r;
		if (r.RecordMatch(99, 100)) return false;
		LOG_INFO(Core, "[ArenaTests] unknown match OK");
		return true;
	}

	bool TestResetWeekly()
	{
		ArenaTeamRegistry r;
		r.AddTeam(ArenaTeam{1, TeamSize::v3, "X", {}, 1500, 0, 0, 0, 0});
		r.AddTeam(ArenaTeam{2, TeamSize::v3, "Y", {}, 1500, 0, 0, 0, 0});
		r.RecordMatch(1, 2);
		auto* x = r.Get(1);
		if (x->weeklyGames != 1 || x->seasonGames != 1) return false;
		r.ResetWeekly();
		x = r.Get(1);
		if (x->weeklyGames != 0) return false;
		if (x->seasonGames != 1) return false; // saison preservee
		LOG_INFO(Core, "[ArenaTests] reset weekly OK");
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

	const bool ok = TestEloEqual() && TestEloFavoriteWins() && TestRecordMatch()
	             && TestUnknownMatch() && TestResetWeekly();
	if (ok) LOG_INFO(Core, "[ArenaTests] ALL OK");
	else LOG_ERROR(Core, "[ArenaTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
