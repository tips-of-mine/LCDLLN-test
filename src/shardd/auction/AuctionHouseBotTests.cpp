#include "src/shardd/auction/AuctionHouseBot.h"
#include "src/shared/core/Log.h"

namespace
{
	using namespace engine::server::auction;

	bool TestNoOpWhenAtTarget()
	{
		AhBotConfig cfg;
		cfg.targetActiveListings = 50;
		cfg.maxListingsPerTick   = 10;
		cfg.seed                 = 42;
		AuctionHouseBot b(cfg);
		auto listings = b.Tick(50);
		if (!listings.empty()) return false;
		listings = b.Tick(100);
		if (!listings.empty()) return false;
		LOG_INFO(Core, "[AhBotTests] noop at target OK");
		return true;
	}

	bool TestRateLimited()
	{
		AhBotConfig cfg;
		cfg.targetActiveListings = 100;
		cfg.maxListingsPerTick   = 5;
		cfg.seed                 = 1;
		AuctionHouseBot b(cfg);
		// 0 actif, target 100, max par tick 5 => 5 listings
		auto listings = b.Tick(0);
		if (listings.size() != 5) return false;
		LOG_INFO(Core, "[AhBotTests] rate limit OK");
		return true;
	}

	bool TestDeficitSmallerThanMax()
	{
		AhBotConfig cfg;
		cfg.targetActiveListings = 50;
		cfg.maxListingsPerTick   = 10;
		cfg.seed                 = 7;
		AuctionHouseBot b(cfg);
		// 47 actif, target 50, max 10 => 3 listings (deficit < max)
		auto listings = b.Tick(47);
		if (listings.size() != 3) return false;
		LOG_INFO(Core, "[AhBotTests] deficit smaller than max OK");
		return true;
	}

	bool TestDeterministicSeed()
	{
		AhBotConfig cfg;
		cfg.targetActiveListings = 100;
		cfg.maxListingsPerTick   = 3;
		cfg.seed                 = 1234;
		AuctionHouseBot b1(cfg);
		AuctionHouseBot b2(cfg);
		auto a = b1.Tick(0);
		auto b = b2.Tick(0);
		if (a.size() != b.size()) return false;
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (a[i].itemTemplateId != b[i].itemTemplateId) return false;
			if (a[i].count          != b[i].count) return false;
			if (a[i].startBidCopper != b[i].startBidCopper) return false;
		}
		LOG_INFO(Core, "[AhBotTests] deterministic seed OK");
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

	const bool ok = TestNoOpWhenAtTarget() && TestRateLimited()
	             && TestDeficitSmallerThanMax() && TestDeterministicSeed();
	if (ok) LOG_INFO(Core, "[AhBotTests] ALL OK");
	else LOG_ERROR(Core, "[AhBotTests] FAIL");
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
