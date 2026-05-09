// CMANGOS.01 (Phase 2.01b) — Tests ChatCommandRouter.
// Pure : aucune dépendance DB.

#include "engine/server/chat/ChatCommandRouter.h"
#include "engine/core/Log.h"

#include <atomic>
#include <string>

namespace
{
	using engine::server::AccountRole;
	using engine::server::chat::ChatCommandRouter;
	using engine::server::chat::CommandDispatchResult;

	bool TestNotACommand()
	{
		ChatCommandRouter r;
		r.Register("/foo", AccountRole::Player, [](uint64_t, std::string_view) {});
		const auto d = r.Dispatch("hello world", 1, AccountRole::Player);
		if (d != CommandDispatchResult::NotACommand)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] expected NotACommand, got {}",
				static_cast<int>(d));
			return false;
		}
		LOG_INFO(Core, "[ChatCommandRouterTests] NotACommand OK");
		return true;
	}

	bool TestUnknownCommand()
	{
		ChatCommandRouter r;
		const auto d = r.Dispatch("/nope arg", 1, AccountRole::Administrator);
		if (d != CommandDispatchResult::UnknownCommand)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] expected UnknownCommand, got {}",
				static_cast<int>(d));
			return false;
		}
		LOG_INFO(Core, "[ChatCommandRouterTests] UnknownCommand OK");
		return true;
	}

	bool TestDispatchOK()
	{
		ChatCommandRouter r;
		std::atomic<int> seenCalls{0};
		std::string seenArgs;
		uint64_t seenId = 0;
		r.Register("/who", AccountRole::Player,
			[&](uint64_t accountId, std::string_view args) {
				++seenCalls;
				seenArgs = std::string(args);
				seenId = accountId;
			});

		const auto d = r.Dispatch("/who   alice  ", 42, AccountRole::Player);
		if (d != CommandDispatchResult::Dispatched)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] expected Dispatched, got {}",
				static_cast<int>(d));
			return false;
		}
		if (seenCalls.load() != 1 || seenId != 42 || seenArgs != "alice")
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] handler params wrong (calls={} id={} args='{}')",
				seenCalls.load(), seenId, seenArgs);
			return false;
		}
		LOG_INFO(Core, "[ChatCommandRouterTests] Dispatch OK + args trimmed");
		return true;
	}

	bool TestCaseInsensitive()
	{
		ChatCommandRouter r;
		std::atomic<int> calls{0};
		r.Register("/Kick", AccountRole::Moderator, [&](uint64_t, std::string_view) { ++calls; });

		// Mod calls /KICK
		const auto d = r.Dispatch("/KICK badguy", 1, AccountRole::Moderator);
		if (d != CommandDispatchResult::Dispatched || calls.load() != 1)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] case-insensitive dispatch failed");
			return false;
		}
		// Vérifie aussi via IsRegistered.
		if (!r.IsRegistered("/kick") || !r.IsRegistered("/KICK") || !r.IsRegistered("kick"))
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] IsRegistered case-insensitive failed");
			return false;
		}
		LOG_INFO(Core, "[ChatCommandRouterTests] case-insensitive OK");
		return true;
	}

	bool TestInsufficientRole()
	{
		ChatCommandRouter r;
		std::atomic<int> calls{0};
		r.Register("/ban", AccountRole::GameMaster,
			[&](uint64_t, std::string_view) { ++calls; });

		// Player tente /ban → refus.
		const auto d = r.Dispatch("/ban alice", 1, AccountRole::Player);
		if (d != CommandDispatchResult::InsufficientRole)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] expected InsufficientRole for Player /ban, got {}",
				static_cast<int>(d));
			return false;
		}
		if (calls.load() != 0)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] handler should NOT have been called");
			return false;
		}

		// Mod tente /ban → toujours refus (minRole=GM).
		const auto d2 = r.Dispatch("/ban alice", 1, AccountRole::Moderator);
		if (d2 != CommandDispatchResult::InsufficientRole)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] expected InsufficientRole for Moderator /ban");
			return false;
		}

		// GM tente /ban → OK.
		const auto d3 = r.Dispatch("/ban alice", 1, AccountRole::GameMaster);
		if (d3 != CommandDispatchResult::Dispatched || calls.load() != 1)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] GM /ban should dispatch");
			return false;
		}

		// Admin tente /ban → OK aussi (rôle plus élevé).
		const auto d4 = r.Dispatch("/ban alice", 1, AccountRole::Administrator);
		if (d4 != CommandDispatchResult::Dispatched || calls.load() != 2)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] Admin /ban should dispatch");
			return false;
		}

		LOG_INFO(Core, "[ChatCommandRouterTests] InsufficientRole OK");
		return true;
	}

	bool TestUnregister()
	{
		ChatCommandRouter r;
		r.Register("/foo", AccountRole::Player, [](uint64_t, std::string_view) {});
		if (!r.IsRegistered("/foo")) return false;
		r.Unregister("/foo");
		if (r.IsRegistered("/foo")) return false;

		// Unregister idempotent.
		r.Unregister("/foo");
		if (r.Size() != 0) return false;

		LOG_INFO(Core, "[ChatCommandRouterTests] Unregister OK");
		return true;
	}

	bool TestSlashOnly()
	{
		ChatCommandRouter r;
		const auto d = r.Dispatch("/", 1, AccountRole::Administrator);
		if (d != CommandDispatchResult::UnknownCommand)
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] '/' alone should be UnknownCommand");
			return false;
		}
		LOG_INFO(Core, "[ChatCommandRouterTests] '/' alone OK");
		return true;
	}

	bool TestOutName()
	{
		ChatCommandRouter r;
		r.Register("/who", AccountRole::Player, [](uint64_t, std::string_view) {});
		std::string seen;
		r.Dispatch("/WHO alice", 1, AccountRole::Player, &seen);
		if (seen != "who")
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] outName expected 'who', got '{}'", seen);
			return false;
		}
		// Même pour UnknownCommand on remplit outName (pratique pour audit).
		std::string seen2;
		r.Dispatch("/Bogus", 1, AccountRole::Player, &seen2);
		if (seen2 != "bogus")
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] outName for unknown expected 'bogus', got '{}'", seen2);
			return false;
		}
		LOG_INFO(Core, "[ChatCommandRouterTests] outName OK");
		return true;
	}

	bool TestEmptyArgs()
	{
		ChatCommandRouter r;
		std::string seenArgs = "<unset>";
		r.Register("/ping", AccountRole::Player,
			[&](uint64_t, std::string_view args) { seenArgs = std::string(args); });
		r.Dispatch("/ping", 1, AccountRole::Player);
		if (seenArgs != "")
		{
			LOG_ERROR(Core, "[ChatCommandRouterTests] /ping no args : expected empty, got '{}'", seenArgs);
			return false;
		}
		LOG_INFO(Core, "[ChatCommandRouterTests] empty args OK");
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

	const bool ok = TestNotACommand()
		&& TestUnknownCommand()
		&& TestDispatchOK()
		&& TestCaseInsensitive()
		&& TestInsufficientRole()
		&& TestUnregister()
		&& TestSlashOnly()
		&& TestOutName()
		&& TestEmptyArgs();

	if (ok)
		LOG_INFO(Core, "[ChatCommandRouterTests] ALL OK");
	else
		LOG_ERROR(Core, "[ChatCommandRouterTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
