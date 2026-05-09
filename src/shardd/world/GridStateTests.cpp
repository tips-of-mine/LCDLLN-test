// CMANGOS.03 (Phase 2.03a) — Tests GridStateTracker.
// Pure logic, pas de DB, pas de threading.

#include "engine/server/GridState.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <chrono>

namespace
{
	using engine::server::CellCoord;
	using engine::server::GridState;
	using engine::server::GridStateConfig;
	using engine::server::GridStateTracker;
	using TP = std::chrono::steady_clock::time_point;
	using namespace std::chrono_literals;

	bool TestEnterMakesActive()
	{
		GridStateTracker t;
		const CellCoord c{1, 2};
		const TP now{};
		t.OnPlayerEnter(c, now);
		if (t.StateOf(c) != GridState::Active)
		{
			LOG_ERROR(Core, "[GridStateTests] expected Active after enter, got {}",
				engine::server::GridStateLabel(t.StateOf(c)));
			return false;
		}
		if (t.PlayerCount(c) != 1)
			return false;
		LOG_INFO(Core, "[GridStateTests] enter -> Active OK");
		return true;
	}

	bool TestLeaveBackToLoaded()
	{
		GridStateTracker t;
		const CellCoord c{0, 0};
		const TP now{};
		t.OnPlayerEnter(c, now);
		t.OnPlayerLeave(c, now);
		// Sans tick, on est revenu à Loaded (pour que le shard arrête de
		// tick), mais pas Idle (pas le délai écoulé).
		if (t.StateOf(c) != GridState::Loaded)
		{
			LOG_ERROR(Core, "[GridStateTests] expected Loaded after leave, got {}",
				engine::server::GridStateLabel(t.StateOf(c)));
			return false;
		}
		if (t.PlayerCount(c) != 0)
			return false;
		LOG_INFO(Core, "[GridStateTests] leave -> Loaded OK");
		return true;
	}

	bool TestMultiplePlayersStaysActive()
	{
		GridStateTracker t;
		const CellCoord c{5, 5};
		const TP now{};
		t.OnPlayerEnter(c, now);
		t.OnPlayerEnter(c, now);
		t.OnPlayerLeave(c, now);
		// Encore 1 joueur → reste Active.
		if (t.StateOf(c) != GridState::Active || t.PlayerCount(c) != 1)
		{
			LOG_ERROR(Core, "[GridStateTests] mult players : expected stay Active");
			return false;
		}
		LOG_INFO(Core, "[GridStateTests] multiple players OK");
		return true;
	}

	bool TestIdleAfterTimeout()
	{
		GridStateConfig cfg;
		cfg.idleTimeout   = 60s;
		cfg.unloadTimeout = 300s;
		GridStateTracker t(cfg);
		const CellCoord c{3, 4};
		const TP t0{};
		t.OnPlayerEnter(c, t0);
		t.OnPlayerLeave(c, t0);

		// 30s plus tard : pas encore Idle.
		t.Tick(t0 + 30s);
		if (t.StateOf(c) != GridState::Loaded)
			return false;

		// 70s plus tard (depuis OnPlayerLeave) : Idle.
		t.Tick(t0 + 70s);
		if (t.StateOf(c) != GridState::Idle)
		{
			LOG_ERROR(Core, "[GridStateTests] expected Idle after 70s, got {}",
				engine::server::GridStateLabel(t.StateOf(c)));
			return false;
		}
		LOG_INFO(Core, "[GridStateTests] Idle after timeout OK");
		return true;
	}

	bool TestRemovalAfterTimeout()
	{
		GridStateConfig cfg;
		cfg.idleTimeout   = 10s;
		cfg.unloadTimeout = 30s;
		GridStateTracker t(cfg);
		const CellCoord c{7, 8};
		const TP t0{};
		t.OnPlayerEnter(c, t0);
		t.OnPlayerLeave(c, t0);

		// Idle après 10s.
		t.Tick(t0 + 15s);
		if (t.StateOf(c) != GridState::Idle) return false;

		// Removal après 30s.
		t.Tick(t0 + 35s);
		if (t.StateOf(c) != GridState::Removal)
		{
			LOG_ERROR(Core, "[GridStateTests] expected Removal after 35s, got {}",
				engine::server::GridStateLabel(t.StateOf(c)));
			return false;
		}
		LOG_INFO(Core, "[GridStateTests] Removal after unloadTimeout OK");
		return true;
	}

	bool TestPlayerReturnsCancelsTimer()
	{
		GridStateConfig cfg;
		cfg.idleTimeout   = 10s;
		cfg.unloadTimeout = 30s;
		GridStateTracker t(cfg);
		const CellCoord c{1, 1};
		const TP t0{};
		t.OnPlayerEnter(c, t0);
		t.OnPlayerLeave(c, t0);

		t.Tick(t0 + 5s);  // pas encore Idle
		t.OnPlayerEnter(c, t0 + 6s);  // joueur revient

		// Doit être Active.
		if (t.StateOf(c) != GridState::Active)
		{
			LOG_ERROR(Core, "[GridStateTests] expected Active after re-enter, got {}",
				engine::server::GridStateLabel(t.StateOf(c)));
			return false;
		}

		// Tick plus tard — toujours Active tant qu'un joueur est là.
		t.Tick(t0 + 100s);
		if (t.StateOf(c) != GridState::Active)
			return false;
		LOG_INFO(Core, "[GridStateTests] re-enter cancels timer OK");
		return true;
	}

	bool TestCellsInState()
	{
		GridStateTracker t;
		const TP t0{};
		t.OnPlayerEnter({0, 0}, t0);
		t.OnPlayerEnter({1, 1}, t0);
		t.OnPlayerEnter({2, 2}, t0);
		t.OnPlayerLeave({1, 1}, t0);

		auto active = t.CellsInState(GridState::Active);
		if (active.size() != 2)
		{
			LOG_ERROR(Core, "[GridStateTests] expected 2 active, got {}", active.size());
			return false;
		}
		auto loaded = t.CellsInState(GridState::Loaded);
		if (loaded.size() != 1)
		{
			LOG_ERROR(Core, "[GridStateTests] expected 1 loaded, got {}", loaded.size());
			return false;
		}
		LOG_INFO(Core, "[GridStateTests] CellsInState OK");
		return true;
	}

	bool TestDrainRemoval()
	{
		GridStateConfig cfg;
		cfg.idleTimeout   = 10s;
		cfg.unloadTimeout = 30s;
		GridStateTracker t(cfg);
		const TP t0{};
		t.OnPlayerEnter({0, 0}, t0);
		t.OnPlayerEnter({1, 1}, t0);
		t.OnPlayerLeave({0, 0}, t0);
		t.OnPlayerLeave({1, 1}, t0);

		t.Tick(t0 + 35s);  // les deux passent en Removal

		auto drained = t.DrainRemovalCells();
		if (drained.size() != 2)
			return false;
		// Après drain : taille à 0.
		if (t.Size() != 0)
		{
			LOG_ERROR(Core, "[GridStateTests] tracker should be empty after drain");
			return false;
		}
		LOG_INFO(Core, "[GridStateTests] DrainRemoval OK");
		return true;
	}

	bool TestStateOfUnknownCell()
	{
		GridStateTracker t;
		// Cellule jamais touchée → Loaded par défaut, count 0.
		if (t.StateOf({99, 99}) != GridState::Loaded) return false;
		if (t.PlayerCount({99, 99}) != 0) return false;
		LOG_INFO(Core, "[GridStateTests] unknown cell defaults OK");
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

	const bool ok = TestEnterMakesActive()
		&& TestLeaveBackToLoaded()
		&& TestMultiplePlayersStaysActive()
		&& TestIdleAfterTimeout()
		&& TestRemovalAfterTimeout()
		&& TestPlayerReturnsCancelsTimer()
		&& TestCellsInState()
		&& TestDrainRemoval()
		&& TestStateOfUnknownCell();

	if (ok)
		LOG_INFO(Core, "[GridStateTests] ALL OK");
	else
		LOG_ERROR(Core, "[GridStateTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
