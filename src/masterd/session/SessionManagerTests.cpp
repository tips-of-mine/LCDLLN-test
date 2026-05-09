/**
 * M20.3: Unit tests for SessionManager state transitions and rules.
 * No external test framework; returns 0 if all pass, non-zero on first failure.
 */

#include "engine/server/SessionManager.h"
#include "engine/core/Config.h"
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
	Assert(mgr.Validate(sid2), "New session valid after SetState");
	mgr.SetState(sid2, SessionState::Active);
	Assert(mgr.Validate(sid2), "New session valid");
}

static void TestExpiry24h()
{
	SessionManager mgr;
	SessionManagerConfig cfg;
	cfg.max_session_age_sec = 1;
	cfg.heartbeat_timeout_sec = 10;
	mgr.SetConfig(cfg);

	uint64_t sid = mgr.CreateSession(1);
	mgr.SetState(sid, SessionState::Active);
	Assert(mgr.Validate(sid), "Validate passes initially");
	std::this_thread::sleep_for(std::chrono::milliseconds(1100));
	mgr.EvictExpired();
	Assert(!mgr.Validate(sid), "Validate fails after max_session_age exceeded");
}

static void TestResumeInWindow()
{
	SessionManager mgr;
	SessionManagerConfig cfg;
	cfg.max_session_age_sec = 3600;
	cfg.heartbeat_timeout_sec = 5;
	cfg.reconnection_window_sec = 5;
	mgr.SetConfig(cfg);

	uint64_t sid = mgr.CreateSession(1);
	mgr.SetState(sid, SessionState::Active);
	Assert(mgr.Validate(sid), "Validate passes");
	Assert(mgr.Touch(sid), "Touch succeeds");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
