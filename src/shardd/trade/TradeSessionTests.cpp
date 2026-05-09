#include "src/shardd/trade/TradeSession.h"
#include "src/shared/core/Log.h"

namespace
{
	using namespace engine::server::trade;

	bool TestHappyPath()
	{
		TradeSession s(1, 2);
		if (s.State() != TradeState::Open) return false;

		TradeOffer oa; oa.items = {100, 101}; oa.copper = 50;
		TradeOffer ob; ob.items = {200};
		if (!s.SetOffer(1, oa)) return false;
		if (!s.SetOffer(2, ob)) return false;

		if (!s.Lock(1)) return false;  // -> LockedA
		if (s.State() != TradeState::LockedA) return false;
		if (!s.Lock(2)) return false;  // -> BothLocked
		if (s.State() != TradeState::BothLocked) return false;

		if (!s.Commit()) return false;
		if (s.State() != TradeState::Committed) return false;
		LOG_INFO(Core, "[TradeTests] happy path OK");
		return true;
	}

	bool TestEditAfterLockRejected()
	{
		TradeSession s(1, 2);
		s.Lock(1);
		TradeOffer o; o.copper = 10;
		if (s.SetOffer(1, o)) return false; // edition apres lock A interdit
		// B peut toujours editer
		if (!s.SetOffer(2, o)) return false;
		LOG_INFO(Core, "[TradeTests] edit after lock rejected OK");
		return true;
	}

	bool TestCommitBeforeBothLocked()
	{
		TradeSession s(1, 2);
		if (s.Commit()) return false;
		s.Lock(1);
		if (s.Commit()) return false;
		s.Lock(2);
		if (!s.Commit()) return false;
		LOG_INFO(Core, "[TradeTests] commit gate OK");
		return true;
	}

	bool TestCancel()
	{
		TradeSession s(1, 2);
		s.Lock(1);
		if (!s.Cancel(2)) return false;
		if (s.State() != TradeState::Cancelled) return false;
		// no-op apres cancel
		if (s.Lock(1)) return false;
		if (s.Commit()) return false;
		LOG_INFO(Core, "[TradeTests] cancel OK");
		return true;
	}

	bool TestUnknownPlayer()
	{
		TradeSession s(1, 2);
		TradeOffer o;
		if (s.SetOffer(99, o)) return false;
		if (s.Lock(99)) return false;
		if (s.Cancel(99)) return false;
		LOG_INFO(Core, "[TradeTests] unknown player rejected OK");
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

	const bool ok = TestHappyPath() && TestEditAfterLockRejected()
	             && TestCommitBeforeBothLocked() && TestCancel() && TestUnknownPlayer();
	if (ok) LOG_INFO(Core, "[TradeTests] ALL OK");
	else LOG_ERROR(Core, "[TradeTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
