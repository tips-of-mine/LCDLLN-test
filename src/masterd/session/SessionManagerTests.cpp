/**
 * M20.3: Unit tests for SessionManager state transitions and rules.
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Clock.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"
#include <chrono>
#include <cstdlib>
#include <iostream>

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

static void TestCreateValidateTouchClose()
{
	SessionManager mgr;
	SessionManagerConfig cfg;
	cfg.max_session_age_sec = 86400;
	cfg.heartbeat_timeout_sec = 300;
	cfg.duplicate_login_policy = DuplicateLoginPolicy::KickExisting;
	mgr.SetConfig(cfg);

	uint64_t sid = mgr.CreateSession(1);
	Assert(sid != 0, "CreateSession returns non-zero");
	Assert(!mgr.Validate(sid), "Validate fails while state Created (not yet Active)");

	mgr.SetState(sid, SessionState::Active);
	Assert(mgr.Validate(sid), "Validate passes after SetState Active");
	Assert(mgr.Touch(sid), "Touch succeeds");
	Assert(mgr.Validate(sid), "Validate still passes after Touch");

	mgr.Close(sid, SessionCloseReason::Logout);
	Assert(!mgr.Validate(sid), "Validate fails after Close");
	Assert(!mgr.Touch(sid), "Touch fails after Close");
}

static void TestDuplicateLoginRefuseNew()
{
	SessionManager mgr;
	SessionManagerConfig cfg;
	cfg.duplicate_login_policy = DuplicateLoginPolicy::RefuseNew;
	mgr.SetConfig(cfg);

	uint64_t sid1 = mgr.CreateSession(42);
	Assert(sid1 != 0, "CreateSession first returns non-zero");
	mgr.SetState(sid1, SessionState::Active);

	uint64_t sid2 = mgr.CreateSession(42);
	Assert(sid2 == 0, "CreateSession duplicate refused (RefuseNew)");
	Assert(mgr.Validate(sid1), "First session still valid");
}

static void TestDuplicateLoginKickExisting()
{
	SessionManager mgr;
	SessionManagerConfig cfg;
	cfg.duplicate_login_policy = DuplicateLoginPolicy::KickExisting;
	mgr.SetConfig(cfg);

	uint64_t sid1 = mgr.CreateSession(42);
	Assert(sid1 != 0, "CreateSession first returns non-zero");
	mgr.SetState(sid1, SessionState::Active);

	uint64_t sid2 = mgr.CreateSession(42);
	Assert(sid2 != 0 && sid2 != sid1, "CreateSession duplicate creates new (KickExisting)");
	Assert(!mgr.Validate(sid1), "Old session invalid after kick");
	// sid2 vient d'être créée en état Created : pas encore Validate-able tant
	// que SetState(Active) n'a pas eu lieu. Le message d'assertion historique
	// "valid after SetState" laissait penser qu'un SetState implicite était fait
	// dans CreateSession, ce qui n'est pas (et n'a jamais été) le cas.
	Assert(!mgr.Validate(sid2), "New session not yet valid (Created, needs SetState)");
	mgr.SetState(sid2, SessionState::Active);
	Assert(mgr.Validate(sid2), "New session valid after SetState Active");
}

static void TestExpiry24h()
{
	// FU-1 : on injecte une FakeClock pour avancer le temps virtuel au-delà
	// de max_session_age_sec=1, sans dépendre du wall-clock. Avant FU-1 ce
	// test faisait `sleep_for(1100ms)` et était exclu de la CI ctest.
	engine::core::FakeClock fakeClock;
	SessionManager mgr;
	mgr.SetClock(&fakeClock);
	SessionManagerConfig cfg;
	cfg.max_session_age_sec = 1;
	cfg.heartbeat_timeout_sec = 10;
	mgr.SetConfig(cfg);

	uint64_t sid = mgr.CreateSession(1);
	mgr.SetState(sid, SessionState::Active);
	Assert(mgr.Validate(sid), "Validate passes initially");
	fakeClock.AdvanceMs(1100);
	mgr.EvictExpired();
	Assert(!mgr.Validate(sid), "Validate fails after max_session_age exceeded");
}

static void TestResumeInWindow()
{
	// FU-1 : FakeClock injectée pour avancer de 100ms sans sleep réel.
	engine::core::FakeClock fakeClock;
	SessionManager mgr;
	mgr.SetClock(&fakeClock);
	SessionManagerConfig cfg;
	cfg.max_session_age_sec = 3600;
	cfg.heartbeat_timeout_sec = 5;
	cfg.reconnection_window_sec = 5;
	mgr.SetConfig(cfg);

	uint64_t sid = mgr.CreateSession(1);
	mgr.SetState(sid, SessionState::Active);
	Assert(mgr.Validate(sid), "Validate passes");
	Assert(mgr.Touch(sid), "Touch succeeds");
	fakeClock.AdvanceMs(100);
	Assert(mgr.Validate(sid), "Validate passes within reconnection/heartbeat window");
}

static void TestLoadConfig()
{
	engine::core::Config config;
	config.SetDefault("session.max_age_sec", static_cast<int64_t>(7200));
	config.SetDefault("session.heartbeat_timeout_sec", static_cast<int64_t>(120));
	config.SetDefault("session.reconnection_window_sec", static_cast<int64_t>(120));
	config.SetDefault("session.duplicate_login_policy", std::string("refuse"));

	SessionManagerConfig cfg = SessionManager::LoadConfig(config);
	Assert(cfg.max_session_age_sec == 7200, "LoadConfig max_age_sec");
	Assert(cfg.heartbeat_timeout_sec == 120, "LoadConfig heartbeat_timeout_sec");
	Assert(cfg.reconnection_window_sec == 120, "LoadConfig reconnection_window_sec");
	Assert(cfg.duplicate_login_policy == DuplicateLoginPolicy::RefuseNew, "LoadConfig duplicate_login_policy refuse");
}

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	engine::core::Log::Init(logSettings);

	TestCreateValidateTouchClose();
	TestDuplicateLoginRefuseNew();
	TestDuplicateLoginKickExisting();
	TestExpiry24h();
	TestResumeInWindow();
	TestLoadConfig();

	engine::core::Log::Shutdown();

	if (s_failCount != 0)
		return static_cast<int>(s_failCount);
	return 0;
}
