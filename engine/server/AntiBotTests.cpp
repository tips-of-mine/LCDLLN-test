/**
 * M33.3 — Unit tests for UserRateLimiter, CaptchaVerifier (bypass mode), and BotDetector.
 * No external test framework; returns 0 if all pass, non-zero on failure.
 */

#include "engine/server/UserRateLimiter.h"
#include "engine/server/CaptchaVerifier.h"
#include "engine/server/BotDetector.h"
#include "engine/core/Log.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace
{
	static int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

using namespace engine::server;
using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// UserRateLimiter tests
// ---------------------------------------------------------------------------

static void TestChatRateLimit()
{
	UserRateLimiter rl;
	UserRateLimiterConfig cfg;
	cfg.chat_per_minute = 3;
	cfg.skills_per_sec  = 100.0;
	rl.SetConfig(cfg);

	const uint64_t user = 42;
	Assert(rl.TryConsumeChat(user), "Chat 1 allowed");
	Assert(rl.TryConsumeChat(user), "Chat 2 allowed");
	Assert(rl.TryConsumeChat(user), "Chat 3 allowed");
	Assert(!rl.TryConsumeChat(user), "Chat 4 rate limited");
	Assert(!rl.TryConsumeChat(user), "Chat 5 rate limited");

	UserRateLimitCounters c;
	rl.GetCounters(c);
	Assert(c.chat_rate_limit_hits >= 2, "Chat counters record rate limit hits");
}

static void TestSkillRateLimit()
{
	UserRateLimiter rl;
	UserRateLimiterConfig cfg;
	cfg.chat_per_minute = 100;
	cfg.skills_per_sec  = 3.0; // very low for test
	rl.SetConfig(cfg);

	const uint64_t user = 99;
	Assert(rl.TryConsumeSkill(user), "Skill 1 allowed");
	Assert(rl.TryConsumeSkill(user), "Skill 2 allowed");
	Assert(rl.TryConsumeSkill(user), "Skill 3 allowed");
	Assert(!rl.TryConsumeSkill(user), "Skill 4 rate limited (bucket empty)");

	UserRateLimitCounters c;
	rl.GetCounters(c);
	Assert(c.skill_rate_limit_hits >= 1, "Skill counters record rate limit hits");
}

static void TestSkillBucketRefill()
{
	UserRateLimiter rl;
	UserRateLimiterConfig cfg;
	cfg.chat_per_minute = 100;
	cfg.skills_per_sec  = 2.0;
	rl.SetConfig(cfg);

	const uint64_t user = 7;
	Assert(rl.TryConsumeSkill(user), "Skill 1 allowed");
	Assert(rl.TryConsumeSkill(user), "Skill 2 allowed");
	Assert(!rl.TryConsumeSkill(user), "Skill 3 rate limited before refill");

	// Wait for bucket to partially refill (1 token in 500 ms with 2/sec rate).
	std::this_thread::sleep_for(std::chrono::milliseconds(600));
	Assert(rl.TryConsumeSkill(user), "Skill allowed after 600ms refill");
}

static void TestUserRateLimiterPurge()
{
	UserRateLimiter rl;
	UserRateLimiterConfig cfg;
	cfg.chat_per_minute = 10;
	cfg.max_entries     = 2;
	rl.SetConfig(cfg);

	rl.TryConsumeChat(1);
	rl.TryConsumeChat(2);
	rl.TryConsumeChat(3);
	// No crash, purge runs within SetConfig limits.
	rl.PurgeExpired(); // should not crash
}

// ---------------------------------------------------------------------------
// CaptchaVerifier tests (bypass mode only — no real HTTP in unit tests)
// ---------------------------------------------------------------------------

static void TestCaptchaBypassMode()
{
	CaptchaVerifier cv;
	CaptchaConfig cfg;
	cfg.enabled = false; // bypass
	cv.SetConfig(cfg);

	Assert(!cv.IsEnabled(), "Bypass mode: not enabled");
	Assert(cv.Verify("any_token", "1.2.3.4"), "Bypass mode accepts any token");
	Assert(cv.Verify("", "1.2.3.4"),           "Bypass mode accepts empty token");
}

static void TestCaptchaEnabledEmptyToken()
{
	CaptchaVerifier cv;
	CaptchaConfig cfg;
	cfg.enabled    = true;
	cfg.secret_key = "test_secret";
	cv.SetConfig(cfg);

	Assert(cv.IsEnabled(), "CAPTCHA enabled");
	// Empty token must be rejected even before HTTP call.
	Assert(!cv.Verify("", "1.2.3.4"), "Empty token rejected when enabled");
}

static void TestCaptchaTrustedIp()
{
	CaptchaVerifier cv;
	CaptchaConfig cfg;
	cfg.enabled    = true;
	cfg.secret_key = "test_secret";
	cfg.trusted_ips.push_back("127.0.0.1");
	cv.SetConfig(cfg);

	// Trusted IP bypasses CAPTCHA even when enabled.
	Assert(cv.Verify("any_token", "127.0.0.1"), "Trusted IP bypasses CAPTCHA");
	// Non-trusted IP with no secret_key would fail; use empty key to test reject path.
	CaptchaVerifier cv2;
	CaptchaConfig cfg2;
	cfg2.enabled    = true;
	cfg2.secret_key = ""; // no secret → reject
	cv2.SetConfig(cfg2);
	Assert(!cv2.Verify("token", "10.0.0.1"), "No secret_key -> token rejected");
}

// ---------------------------------------------------------------------------
// BotDetector tests
// ---------------------------------------------------------------------------

static void TestBotDetectorClean()
{
	BotDetector bd;
	BotDetectorConfig cfg;
	cfg.window_size    = 10;
	cfg.min_action_interval_ms = 100.0;
	cfg.flag_threshold = 3;
	cfg.autoban_threshold = 5;
	bd.SetConfig(cfg);

	const uint64_t user = 1001;
	// Actions with large gaps (> 200ms) should not trigger anomalies.
	auto t = Clock::now();
	for (int i = 0; i < 5; ++i)
	{
		t += std::chrono::milliseconds(300 + i * 50); // varying, human-like
		const auto sig = bd.RecordAction(user, BotActionType::SkillUse, t);
		Assert(sig == BotSignal::Clean || sig == BotSignal::Suspicious,
			"Clean/slow actions produce no autoban");
	}
	Assert(!bd.ShouldAutoBan(user), "No autoban for clean actions");
}

static void TestBotDetectorImpossibleSpeed()
{
	BotDetector bd;
	BotDetectorConfig cfg;
	cfg.window_size               = 10;
	cfg.min_action_interval_ms    = 100.0;
	cfg.flag_threshold            = 1;
	cfg.autoban_threshold         = 5;
	cfg.regularity_variance_threshold_ms2 = 9999.0; // disable variance check
	bd.SetConfig(cfg);

	const uint64_t user = 2002;
	auto t = Clock::now();
	// First action OK.
	bd.RecordAction(user, BotActionType::SkillUse, t);
	// Second action 10ms later — below 100ms threshold → suspicious.
	t += std::chrono::milliseconds(10);
	const auto sig = bd.RecordAction(user, BotActionType::SkillUse, t);
	Assert(sig == BotSignal::Suspicious || sig == BotSignal::AutoBan,
		"Impossible speed detected as suspicious");
	Assert(bd.IsSuspicious(user), "User flagged after impossible speed");
}

static void TestBotDetectorAutoban()
{
	BotDetector bd;
	BotDetectorConfig cfg;
	cfg.window_size               = 20;
	cfg.min_action_interval_ms    = 100.0;
	cfg.flag_threshold            = 1;
	cfg.autoban_threshold         = 3;
	cfg.regularity_variance_threshold_ms2 = 9999.0;
	bd.SetConfig(cfg);

	const uint64_t user = 3003;
	auto t = Clock::now();
	BotSignal last = BotSignal::Clean;
	// Send 5 actions with 5ms gaps — all below min_action_interval_ms.
	for (int i = 0; i < 5; ++i)
	{
		t += std::chrono::milliseconds(5);
		last = bd.RecordAction(user, BotActionType::SkillUse, t);
	}
	// Should have hit autoban threshold after 3 suspicious events.
	Assert(last == BotSignal::AutoBan || bd.ShouldAutoBan(user),
		"AutoBan threshold reached after repeated violations");
}

static void TestBotDetectorClearFlag()
{
	BotDetector bd;
	BotDetectorConfig cfg;
	cfg.window_size            = 10;
	cfg.min_action_interval_ms = 100.0;
	cfg.flag_threshold         = 1;
	cfg.autoban_threshold      = 0; // disabled
	cfg.regularity_variance_threshold_ms2 = 9999.0;
	bd.SetConfig(cfg);

	const uint64_t user = 4004;
	auto t = Clock::now();
	bd.RecordAction(user, BotActionType::SkillUse, t);
	t += std::chrono::milliseconds(5);
	bd.RecordAction(user, BotActionType::SkillUse, t);
	Assert(bd.IsSuspicious(user), "User flagged");

	bd.ClearFlag(user);
	Assert(!bd.IsSuspicious(user),   "Flag cleared");
	Assert(!bd.ShouldAutoBan(user),  "No autoban after clear");
}

static void TestBotDetectorPurge()
{
	BotDetector bd;
	BotDetectorConfig cfg;
	cfg.window_size    = 5;
	cfg.idle_purge_sec = 1; // purge after 1 second idle
	bd.SetConfig(cfg);

	const uint64_t user = 5005;
	auto t = Clock::now();
	bd.RecordAction(user, BotActionType::ChatSend, t);

	// Let the entry become stale and purge it.
	std::this_thread::sleep_for(std::chrono::milliseconds(1100));
	bd.PurgeExpired(); // should not crash
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	engine::core::Log::Init(logSettings);

	TestChatRateLimit();
	TestSkillRateLimit();
	TestSkillBucketRefill();
	TestUserRateLimiterPurge();
	TestCaptchaBypassMode();
	TestCaptchaEnabledEmptyToken();
	TestCaptchaTrustedIp();
	TestBotDetectorClean();
	TestBotDetectorImpossibleSpeed();
	TestBotDetectorAutoban();
	TestBotDetectorClearFlag();
	TestBotDetectorPurge();

	engine::core::Log::Shutdown();

	if (s_failCount != 0)
		return static_cast<int>(s_failCount);
	return 0;
}
