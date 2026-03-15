/**
 * M20.4: Unit tests for rate limiting and IP ban. No external test framework; returns 0 if all pass.
 */

#include "engine/server/RateLimitAndBan.h"
#include "engine/server/SecurityAuditLog.h"
#include "engine/core/Log.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
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

static void TestRateLimitAuth()
{
	RateLimitAndBan rlb;
	RateLimitAndBanConfig cfg;
	cfg.auth_per_minute = 3;
	cfg.register_per_hour = 10;
	rlb.SetConfig(cfg);

	const std::string ip = "192.168.1.1";
	Assert(rlb.TryConsumeAuth(ip), "Auth 1 allowed");
	Assert(rlb.TryConsumeAuth(ip), "Auth 2 allowed");
	Assert(rlb.TryConsumeAuth(ip), "Auth 3 allowed");
	Assert(!rlb.TryConsumeAuth(ip), "Auth 4 rate limited");
	Assert(!rlb.TryConsumeAuth(ip), "Auth 5 rate limited");

	SecurityCounters c;
	rlb.GetCounters(c);
	Assert(c.rate_limit_hits_auth >= 2, "Counters record rate limit hits");
}

static void TestRateLimitRegister()
{
	RateLimitAndBan rlb;
	RateLimitAndBanConfig cfg;
	cfg.auth_per_minute = 100;
	cfg.register_per_hour = 2;
	rlb.SetConfig(cfg);

	const std::string ip = "10.0.0.1";
	Assert(rlb.TryConsumeRegister(ip), "Register 1 allowed");
	Assert(rlb.TryConsumeRegister(ip), "Register 2 allowed");
	Assert(!rlb.TryConsumeRegister(ip), "Register 3 rate limited");
}

static void TestIpBanTrigger()
{
	RateLimitAndBan rlb;
	RateLimitAndBanConfig cfg;
	cfg.auth_per_minute = 100;
	cfg.max_failures_before_ban = 3;
	cfg.ban_duration_sec = 1;
	rlb.SetConfig(cfg);

	const std::string ip = "172.16.0.1";
	Assert(!rlb.IsBanned(ip), "Not banned initially");
	rlb.RecordAuthFailure(ip);
	rlb.RecordAuthFailure(ip);
	Assert(!rlb.IsBanned(ip), "Not banned after 2 failures");
	rlb.RecordAuthFailure(ip);
	Assert(rlb.IsBanned(ip), "Banned after 3 failures");
	Assert(!rlb.TryConsumeAuth(ip), "Auth refused when banned");

	SecurityCounters c;
	rlb.GetCounters(c);
	Assert(c.bans_issued == 1, "Counters record ban issued");
}

static void TestIpBanExpiry()
{
	RateLimitAndBan rlb;
	RateLimitAndBanConfig cfg;
	cfg.max_failures_before_ban = 1;
	cfg.ban_duration_sec = 1;
	rlb.SetConfig(cfg);

	const std::string ip = "127.0.0.1";
	rlb.RecordAuthFailure(ip);
	Assert(rlb.IsBanned(ip), "Banned after 1 failure");
	std::this_thread::sleep_for(std::chrono::milliseconds(1100));
	rlb.PurgeExpired();
	Assert(!rlb.IsBanned(ip), "Not banned after expiry and purge");
}

static void TestAuditLogContainsEvents()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	engine::core::Log::Init(logSettings);

	SecurityAuditLog audit;
	std::string path = "security_audit_test.log";
	Assert(audit.Init(path), "Audit Init");
	audit.LogLoginSuccess("1.2.3.4", 42, 100);
	audit.LogLoginFail("1.2.3.4", "invalid_credentials");
	audit.LogBan("1.2.3.4", "too_many_failures");
	audit.LogSessionCreated(100, 42);
	audit.LogSessionClosed(100, "logout");
	audit.Shutdown();

	std::ifstream f(path);
	Assert(f.good(), "Audit file readable");
	std::stringstream buf;
	buf << f.rdbuf();
	std::string content = buf.str();
	Assert(content.find("LOGIN_SUCCESS") != std::string::npos, "Audit contains LOGIN_SUCCESS");
	Assert(content.find("LOGIN_FAIL") != std::string::npos, "Audit contains LOGIN_FAIL");
	Assert(content.find("BAN") != std::string::npos, "Audit contains BAN");
	Assert(content.find("SESSION_CREATED") != std::string::npos, "Audit contains SESSION_CREATED");
	Assert(content.find("SESSION_CLOSED") != std::string::npos, "Audit contains SESSION_CLOSED");
	f.close();
	std::remove(path.c_str());

	engine::core::Log::Shutdown();
}

int main()
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Off;
	engine::core::Log::Init(logSettings);

	TestRateLimitAuth();
	TestRateLimitRegister();
	TestIpBanTrigger();
	TestIpBanExpiry();
	TestAuditLogContainsEvents();

	engine::core::Log::Shutdown();

	if (s_failCount != 0)
		return static_cast<int>(s_failCount);
	return 0;
}
