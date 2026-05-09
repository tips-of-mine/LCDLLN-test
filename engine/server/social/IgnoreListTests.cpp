// CMANGOS.25 (Phase 3.25a) — Tests IgnoreListManager.

#include "engine/server/social/IgnoreList.h"
#include "engine/core/Log.h"

namespace
{
	using engine::server::social::IgnoreListManager;
	using engine::server::social::IgnoreOpResult;
	using engine::server::social::InMemoryIgnoreStore;
	using engine::server::social::kMaxIgnoredPerAccount;

	bool TestBasic()
	{
		InMemoryIgnoreStore store;
		IgnoreListManager mgr(&store);

		if (mgr.Ignore(1, 2) != IgnoreOpResult::OK) return false;
		if (!mgr.IsIgnored(1, 2)) return false;
		if (mgr.IsIgnored(1, 3)) return false;
		if (mgr.IsIgnored(2, 1)) return false;  // pas symetrique

		auto list = mgr.List(1);
		if (list.size() != 1 || list[0] != 2) return false;
		LOG_INFO(Core, "[IgnoreListTests] basic OK");
		return true;
	}

	bool TestAlreadyIgnored()
	{
		InMemoryIgnoreStore store;
		IgnoreListManager mgr(&store);
		mgr.Ignore(1, 2);
		if (mgr.Ignore(1, 2) != IgnoreOpResult::AlreadyIgnored) return false;
		LOG_INFO(Core, "[IgnoreListTests] AlreadyIgnored OK");
		return true;
	}

	bool TestSelfIgnore()
	{
		InMemoryIgnoreStore store;
		IgnoreListManager mgr(&store);
		if (mgr.Ignore(1, 1) != IgnoreOpResult::SelfIgnore) return false;
		LOG_INFO(Core, "[IgnoreListTests] SelfIgnore OK");
		return true;
	}

	bool TestUnignore()
	{
		InMemoryIgnoreStore store;
		IgnoreListManager mgr(&store);
		mgr.Ignore(1, 2);
		if (mgr.Unignore(1, 2) != IgnoreOpResult::OK) return false;
		if (mgr.IsIgnored(1, 2)) return false;
		// Re-Unignore → NotIgnored.
		if (mgr.Unignore(1, 2) != IgnoreOpResult::NotIgnored) return false;
		LOG_INFO(Core, "[IgnoreListTests] Unignore OK");
		return true;
	}

	bool TestListFullCap()
	{
		InMemoryIgnoreStore store;
		IgnoreListManager mgr(&store);
		// Remplir jusqu'a la limite.
		for (size_t i = 0; i < kMaxIgnoredPerAccount; ++i)
		{
			if (mgr.Ignore(1, 100 + i) != IgnoreOpResult::OK) return false;
		}
		// Le suivant doit echouer.
		if (mgr.Ignore(1, 9999) != IgnoreOpResult::ListFull) return false;
		LOG_INFO(Core, "[IgnoreListTests] cap at {} OK", kMaxIgnoredPerAccount);
		return true;
	}

	bool TestPerOwnerIsolation()
	{
		InMemoryIgnoreStore store;
		IgnoreListManager mgr(&store);
		mgr.Ignore(1, 100);
		mgr.Ignore(2, 200);

		if (!mgr.IsIgnored(1, 100)) return false;
		if (mgr.IsIgnored(1, 200)) return false;
		if (!mgr.IsIgnored(2, 200)) return false;
		if (mgr.IsIgnored(2, 100)) return false;

		LOG_INFO(Core, "[IgnoreListTests] per-owner isolation OK");
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

	const bool ok = TestBasic() && TestAlreadyIgnored() && TestSelfIgnore()
		&& TestUnignore() && TestListFullCap() && TestPerOwnerIsolation();

	if (ok) LOG_INFO(Core, "[IgnoreListTests] ALL OK");
	else LOG_ERROR(Core, "[IgnoreListTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
