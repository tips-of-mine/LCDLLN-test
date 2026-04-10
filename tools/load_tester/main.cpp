// M25.1 — Load tester (TLS): connect-only / auth-only / heartbeat-only / mix

#include "LoadTester.h"

#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace
{
	using tools::load_tester::LoadTestConfig;
	using tools::load_tester::LoadTester;
	using tools::load_tester::Scenario;

	bool ParseUInt32(const char* s, uint32_t& out)
	{
		if (!s)
			return false;
		try
		{
			int64_t v = std::stoll(s);
			if (v < 0)
				return false;
			out = static_cast<uint32_t>(v);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool ParseUInt16(const char* s, uint16_t& out)
	{
		if (!s)
			return false;
		try
		{
			int64_t v = std::stoll(s);
			if (v < 0 || v > 65535)
				return false;
			out = static_cast<uint16_t>(v);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool ParseFloat(const char* s, float& out)
	{
		if (!s)
			return false;
		try
		{
			out = std::stof(s);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	Scenario ParseScenario(std::string_view s, bool& ok)
	{
		ok = true;
		if (s == "connect-only")
			return Scenario::ConnectOnly;
		if (s == "auth-only")
			return Scenario::AuthOnly;
		if (s == "heartbeat-only")
			return Scenario::HeartbeatOnly;
		if (s == "mix")
			return Scenario::Mix;
		ok = false;
		return Scenario::Mix;
	}

}

int main(int argc, char** argv)
{
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	logSettings.flushAlways = true;
	logSettings.filePath = "";
	engine::core::Log::Init(logSettings);

	LOG_INFO(LoadTester, "[LoadTester] Boot started");

	// Load base defaults from config.json (if present); CLI overrides take precedence if provided as --key=value.
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);

	LoadTestConfig cfg;
	cfg.masterHost = config.GetEffectiveMasterHost("localhost");
	cfg.masterPort = static_cast<uint16_t>(config.GetInt("client.master_port", 3840));
	cfg.login = config.GetString("client.login", "testuser");
	cfg.clientHash = config.GetString("client.client_hash", "");
	cfg.serverFingerprintHex = config.GetString("client.server_fingerprint", "");

	cfg.clients = static_cast<uint32_t>(config.GetInt("load_tester.clients", 1000));
	cfg.rampUpSec = static_cast<float>(config.GetDouble("load_tester.ramp_up_sec", 10.0));
	cfg.durationSec = static_cast<uint32_t>(config.GetInt("load_tester.duration_sec", 30));
	cfg.instances = static_cast<uint32_t>(config.GetInt("load_tester.instances", 1));
	cfg.instanceIndex = static_cast<uint32_t>(config.GetInt("load_tester.instance_index", 0));
	cfg.allowInsecureDev = config.GetBool("load_tester.allow_insecure_dev", false);

	// CLI overrides (explicit flags).
	for (int i = 1; i < argc; ++i)
	{
		std::string_view arg = argv[i] ? argv[i] : "";
		auto next = [&](int& j) -> const char*
		{
			if (j + 1 >= argc)
				return nullptr;
			return argv[++j];
		};

		if (arg == "--help" || arg == "-h")
		{
			LOG_INFO(LoadTester, "Usage:");
			LOG_INFO(LoadTester, "  load_tester --scenario <connect-only|auth-only|heartbeat-only|mix>");
			LOG_INFO(LoadTester, "             --server-fingerprint <sha256_hex> [--allow-insecure-dev]");
			LOG_INFO(LoadTester, "             --clients <N> --ramp-up-sec <sec> --duration-sec <sec>");
			LOG_INFO(LoadTester, "             --master-host <host> --master-port <port>");
			LOG_INFO(LoadTester, "             --login <user> --client-hash <hash> (auth/heartbeat scenarios)");
			return 0;
		}
		else if (arg == "--scenario")
		{
			const char* s = next(i);
			if (!s)
			{
				LOG_ERROR(LoadTester, "[LoadTester] Missing value for --scenario");
				engine::core::Log::Shutdown();
				return 1;
			}
			bool ok = false;
			cfg.scenario = ParseScenario(s, ok);
			if (!ok)
			{
				LOG_ERROR(LoadTester, "[LoadTester] Invalid --scenario='{}'", s);
				engine::core::Log::Shutdown();
				return 1;
			}
		}
		else if (arg == "--clients")
		{
			const char* s = next(i);
			uint32_t v = 0;
			if (!ParseUInt32(s, v))
			{
				LOG_ERROR(LoadTester, "[LoadTester] Invalid --clients value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.clients = v;
		}
		else if (arg == "--ramp-up-sec")
		{
			const char* s = next(i);
			float v = 0.0f;
			if (!ParseFloat(s, v))
			{
				LOG_ERROR(LoadTester, "[LoadTester] Invalid --ramp-up-sec value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.rampUpSec = v;
		}
		else if (arg == "--duration-sec")
		{
			const char* s = next(i);
			uint32_t v = 0;
			if (!ParseUInt32(s, v))
			{
				LOG_ERROR(LoadTester, "[LoadTester] Invalid --duration-sec value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.durationSec = v;
		}
		else if (arg == "--instances")
		{
			const char* s = next(i);
			uint32_t v = 1;
			if (!ParseUInt32(s, v))
			{
				LOG_ERROR(LoadTester, "[LoadTester] Invalid --instances value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.instances = v;
		}
		else if (arg == "--instance-index")
		{
			const char* s = next(i);
			uint32_t v = 0;
			if (!ParseUInt32(s, v))
			{
				LOG_ERROR(LoadTester, "[LoadTester] Invalid --instance-index value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.instanceIndex = v;
		}
		else if (arg == "--master-host")
		{
			const char* s = next(i);
			if (!s)
			{
				LOG_ERROR(LoadTester, "[LoadTester] Missing --master-host value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.masterHost = s;
		}
		else if (arg == "--master-port")
		{
			const char* s = next(i);
			uint16_t v = 0;
			if (!ParseUInt16(s, v))
			{
				LOG_ERROR(LoadTester, "[LoadTester] Invalid --master-port value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.masterPort = v;
		}
		else if (arg == "--login")
		{
			const char* s = next(i);
			if (!s)
			{
				LOG_ERROR(LoadTester, "[LoadTester] Missing --login value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.login = s;
		}
		else if (arg == "--client-hash")
		{
			const char* s = next(i);
			if (!s)
			{
				LOG_ERROR(LoadTester, "[LoadTester] Missing --client-hash value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.clientHash = s;
		}
		else if (arg == "--server-fingerprint")
		{
			const char* s = next(i);
			if (!s)
			{
				LOG_ERROR(LoadTester, "[LoadTester] Missing --server-fingerprint value");
				engine::core::Log::Shutdown();
				return 1;
			}
			cfg.serverFingerprintHex = s;
		}
		else if (arg == "--allow-insecure-dev")
		{
			cfg.allowInsecureDev = true;
		}
		else
		{
			LOG_WARN(LoadTester, "[LoadTester] Unknown arg='{}' (ignored)", arg);
		}
	}

	{
		LoadTester tester(cfg);
		const bool ok = tester.Run();
		engine::core::Log::Shutdown();
		return ok ? 0 : 1;
	}
}

