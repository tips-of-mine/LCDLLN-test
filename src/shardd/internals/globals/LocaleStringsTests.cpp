// CMANGOS.16 (Phase 1b) — Tests LocaleStrings.

#include "engine/server/shard/globals/LocaleStrings.h"
#include "engine/server/db/ConnectionPool.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::shard::globals::LocaleStrings;
	using engine::server::db::ConnectionPool;

	bool TestGetAndFallback(LocaleStrings& mgr)
	{
		// Seeds : ID 1000 fr_FR + en_US ; ID 1001 fr_FR seul.
		// fr_FR = locale_id 0, en_US = locale_id 1.

		const std::string fr1000 = mgr.GetString(1000, 0);
		if (fr1000 != "Bienvenue {0}, niveau {1}!")
		{
			LOG_ERROR(Core, "[LocaleStringsTests] GetString(1000, fr_FR) unexpected: {}", fr1000);
			return false;
		}
		const std::string en1000 = mgr.GetString(1000, 1);
		if (en1000 != "Welcome {0}, level {1}!")
		{
			LOG_ERROR(Core, "[LocaleStringsTests] GetString(1000, en_US) unexpected: {}", en1000);
			return false;
		}
		// ID 1001 en_US absent → fallback sur fr_FR (default).
		const std::string en1001 = mgr.GetString(1001, 1);
		if (en1001 != "Bonjour le monde")
		{
			LOG_ERROR(Core, "[LocaleStringsTests] GetString(1001, en_US) fallback failed: {}", en1001);
			return false;
		}
		// ID inexistant → sentinel.
		const std::string none = mgr.GetString(9999, 0);
		if (none.find("9999") == std::string::npos)
		{
			LOG_ERROR(Core, "[LocaleStringsTests] GetString(9999) sentinel missing: {}", none);
			return false;
		}
		LOG_INFO(Core, "[LocaleStringsTests] Get + fallback + sentinel OK");
		return true;
	}

	bool TestFormat(LocaleStrings& mgr)
	{
		const std::string s = mgr.Format(1000, 0, "Hortense", "42");
		if (s != "Bienvenue Hortense, niveau 42!")
		{
			LOG_ERROR(Core, "[LocaleStringsTests] Format unexpected: {}", s);
			return false;
		}
		LOG_INFO(Core, "[LocaleStringsTests] Format placeholders OK");
		return true;
	}
}

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	engine::core::Log::Init(logSettings);

	if (config.GetString("db.host", "").empty())
	{
		LOG_INFO(Core, "[LocaleStringsTests] db.host not set, skipping");
		engine::core::Log::Shutdown();
		return 0;
	}

	ConnectionPool pool;
	if (!pool.Init(config))
	{
		engine::core::Log::Shutdown();
		return 1;
	}

	LocaleStrings mgr;
	bool ok = mgr.Load(pool, 0) && TestGetAndFallback(mgr) && TestFormat(mgr);

	pool.Shutdown();
	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
