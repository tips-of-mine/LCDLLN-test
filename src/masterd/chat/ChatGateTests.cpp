// CMANGOS.01 (Phase 2.01a) — Tests ChatGate.
// Pas de dépendance MySQL : on injecte des callbacks mock pour simuler
// la DB et le store. Couvre : ban / mute permanent / mute expiré /
// anti-flood / sliding window / Decide vs DecideAndRecord.

#include "src/masterd/chat/ChatGate.h"
#include "src/shared/core/Log.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace
{
	using engine::server::chat::ChatGate;
	using engine::server::chat::ChatGateConfig;
	using engine::server::chat::ChatGateDecision;
	using engine::server::chat::ChatMute;

	bool TestAllowedDefault()
	{
		ChatGate g;
		const auto r = g.DecideAndRecord(42, 1000);
		if (r.decision != ChatGateDecision::Allowed)
		{
			LOG_ERROR(Core, "[ChatGateTests] expected Allowed (no callbacks), got decision={}",
				static_cast<int>(r.decision));
			return false;
		}
		LOG_INFO(Core, "[ChatGateTests] default Allowed OK");
		return true;
	}

	bool TestBannedShortCircuits()
	{
		ChatGate g;
		g.SetBannedCheck([](uint64_t id) { return id == 7; });
		// Mute aussi câblé : on vérifie que le ban a la priorité.
		g.SetMuteLookup([](uint64_t) -> std::optional<ChatMute> {
			ChatMute m; m.untilTsMs = 0; m.reason = "should not surface";
			return m;
		});

		const auto r = g.DecideAndRecord(7, 1000);
		if (r.decision != ChatGateDecision::Banned)
		{
			LOG_ERROR(Core, "[ChatGateTests] expected Banned, got {}", static_cast<int>(r.decision));
			return false;
		}
		if (!r.reason.empty())
		{
			LOG_ERROR(Core, "[ChatGateTests] Banned should have empty reason, got '{}'", r.reason);
			return false;
		}

		// Compte non banni → pas Banned (mute persiste).
		const auto r2 = g.DecideAndRecord(8, 1000);
		if (r2.decision != ChatGateDecision::Muted)
		{
			LOG_ERROR(Core, "[ChatGateTests] non-banned account should fall through to Muted, got {}",
				static_cast<int>(r2.decision));
			return false;
		}
		LOG_INFO(Core, "[ChatGateTests] ban short-circuit OK");
		return true;
	}

	bool TestMutePermanent()
	{
		ChatGate g;
		g.SetMuteLookup([](uint64_t id) -> std::optional<ChatMute> {
			if (id != 99) return std::nullopt;
			ChatMute m; m.untilTsMs = 0; m.reason = "spam"; return m;
		});

		const auto r = g.DecideAndRecord(99, 1000);
		if (r.decision != ChatGateDecision::Muted)
		{
			LOG_ERROR(Core, "[ChatGateTests] expected Muted permanent, got {}", static_cast<int>(r.decision));
			return false;
		}
		if (r.reason != "spam" || r.untilTsMs != 0)
		{
			LOG_ERROR(Core, "[ChatGateTests] mute fields wrong (reason='{}' until={})", r.reason, r.untilTsMs);
			return false;
		}
		LOG_INFO(Core, "[ChatGateTests] mute permanent OK");
		return true;
	}

	bool TestMuteExpired()
	{
		ChatGate g;
		// Mute jusqu'à t=500. On query à t=1000 → expiré → fallthrough.
		g.SetMuteLookup([](uint64_t) -> std::optional<ChatMute> {
			ChatMute m; m.untilTsMs = 500; m.reason = "old"; return m;
		});

		const auto r = g.DecideAndRecord(1, 1000);
		if (r.decision != ChatGateDecision::Allowed)
		{
			LOG_ERROR(Core, "[ChatGateTests] expected Allowed (mute expired), got {}",
				static_cast<int>(r.decision));
			return false;
		}
		LOG_INFO(Core, "[ChatGateTests] mute expired OK");
		return true;
	}

	bool TestMuteActive()
	{
		ChatGate g;
		// Mute jusqu'à t=2000. Query à t=1000 → encore actif.
		g.SetMuteLookup([](uint64_t) -> std::optional<ChatMute> {
			ChatMute m; m.untilTsMs = 2000; m.reason = "warning"; return m;
		});

		const auto r = g.DecideAndRecord(1, 1000);
		if (r.decision != ChatGateDecision::Muted || r.untilTsMs != 2000)
		{
			LOG_ERROR(Core, "[ChatGateTests] mute active failed (decision={} until={})",
				static_cast<int>(r.decision), r.untilTsMs);
			return false;
		}
		LOG_INFO(Core, "[ChatGateTests] mute active OK");
		return true;
	}

	bool TestAntiFlood()
	{
		ChatGateConfig cfg;
		cfg.floodWindowMs    = 1000;
		cfg.floodMaxMessages = 3;
		ChatGate g(cfg);

		// 3 messages dans la fenêtre : tous OK.
		for (int i = 0; i < 3; ++i)
		{
			const auto r = g.DecideAndRecord(42, 100 + static_cast<uint64_t>(i) * 100);
			if (r.decision != ChatGateDecision::Allowed)
			{
				LOG_ERROR(Core, "[ChatGateTests] msg #{} should be Allowed, got {}",
					i, static_cast<int>(r.decision));
				return false;
			}
		}

		// 4ᵉ message à t=400 (dans la fenêtre) → Flooding.
		const auto r4 = g.DecideAndRecord(42, 400);
		if (r4.decision != ChatGateDecision::Flooding)
		{
			LOG_ERROR(Core, "[ChatGateTests] msg #4 should be Flooding, got {}",
				static_cast<int>(r4.decision));
			return false;
		}

		// À t=1500, fenêtre = [500, 1500]. Les 3 messages (100, 200, 300)
		// sont hors fenêtre → tous purgés → Allowed.
		const auto r5 = g.DecideAndRecord(42, 1500);
		if (r5.decision != ChatGateDecision::Allowed)
		{
			LOG_ERROR(Core, "[ChatGateTests] after window expiry should be Allowed, got {}",
				static_cast<int>(r5.decision));
			return false;
		}

		LOG_INFO(Core, "[ChatGateTests] anti-flood OK");
		return true;
	}

	bool TestDecideDoesNotMutate()
	{
		ChatGateConfig cfg;
		cfg.floodWindowMs    = 1000;
		cfg.floodMaxMessages = 2;
		ChatGate g(cfg);

		// 100 calls à Decide : ne doit jamais incrémenter la fenêtre.
		for (int i = 0; i < 100; ++i)
		{
			const auto r = g.Decide(7, 100);
			if (r.decision != ChatGateDecision::Allowed)
			{
				LOG_ERROR(Core, "[ChatGateTests] Decide should be pure Allowed, got {} on iter {}",
					static_cast<int>(r.decision), i);
				return false;
			}
		}

		// Maintenant 2 DecideAndRecord → Allowed, le 3ᵉ → Flooding.
		if (g.DecideAndRecord(7, 200).decision != ChatGateDecision::Allowed)
		{
			LOG_ERROR(Core, "[ChatGateTests] 1st record should be Allowed");
			return false;
		}
		if (g.DecideAndRecord(7, 201).decision != ChatGateDecision::Allowed)
		{
			LOG_ERROR(Core, "[ChatGateTests] 2nd record should be Allowed");
			return false;
		}
		if (g.DecideAndRecord(7, 202).decision != ChatGateDecision::Flooding)
		{
			LOG_ERROR(Core, "[ChatGateTests] 3rd record should be Flooding");
			return false;
		}

		LOG_INFO(Core, "[ChatGateTests] Decide pure (no state mutation) OK");
		return true;
	}

	bool TestPerAccountIsolation()
	{
		ChatGateConfig cfg;
		cfg.floodWindowMs    = 1000;
		cfg.floodMaxMessages = 1;
		ChatGate g(cfg);

		// account 1 envoie un message → Allowed.
		if (g.DecideAndRecord(1, 100).decision != ChatGateDecision::Allowed)
		{
			LOG_ERROR(Core, "[ChatGateTests] account 1 first msg should be Allowed");
			return false;
		}
		// account 2 envoie un message → Allowed (compteur séparé).
		if (g.DecideAndRecord(2, 100).decision != ChatGateDecision::Allowed)
		{
			LOG_ERROR(Core, "[ChatGateTests] account 2 first msg should be Allowed");
			return false;
		}
		// account 1 deuxième message → Flooding (max=1).
		if (g.DecideAndRecord(1, 100).decision != ChatGateDecision::Flooding)
		{
			LOG_ERROR(Core, "[ChatGateTests] account 1 second msg should be Flooding");
			return false;
		}
		// account 2 deuxième message → Flooding aussi (mais indépendant).
		if (g.DecideAndRecord(2, 100).decision != ChatGateDecision::Flooding)
		{
			LOG_ERROR(Core, "[ChatGateTests] account 2 second msg should be Flooding");
			return false;
		}

		LOG_INFO(Core, "[ChatGateTests] per-account isolation OK");
		return true;
	}

	bool TestResetState()
	{
		ChatGateConfig cfg;
		cfg.floodWindowMs    = 1000;
		cfg.floodMaxMessages = 1;
		ChatGate g(cfg);

		g.DecideAndRecord(1, 100);  // Allowed
		if (g.DecideAndRecord(1, 100).decision != ChatGateDecision::Flooding)
			return false;

		g.ResetState();

		if (g.DecideAndRecord(1, 100).decision != ChatGateDecision::Allowed)
		{
			LOG_ERROR(Core, "[ChatGateTests] after Reset should be Allowed");
			return false;
		}

		LOG_INFO(Core, "[ChatGateTests] ResetState OK");
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

	const bool ok = TestAllowedDefault()
		&& TestBannedShortCircuits()
		&& TestMutePermanent()
		&& TestMuteExpired()
		&& TestMuteActive()
		&& TestAntiFlood()
		&& TestDecideDoesNotMutate()
		&& TestPerAccountIsolation()
		&& TestResetState();

	if (ok)
		LOG_INFO(Core, "[ChatGateTests] ALL OK");
	else
		LOG_ERROR(Core, "[ChatGateTests] FAIL");

	engine::core::Log::Shutdown();
	return ok ? 0 : 1;
}
