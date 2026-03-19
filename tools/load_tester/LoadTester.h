#pragma once

#include "engine/network/NetClient.h"
#include "engine/network/RequestResponseDispatcher.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tools::load_tester
{
	using engine::network::NetClient;
	using engine::network::NetClientState;
	using engine::network::RequestResponseDispatcher;

	enum class Scenario
	{
		ConnectOnly,
		AuthOnly,
		HeartbeatOnly,
		Mix
	};

	struct LoadTestConfig
	{
		std::string masterHost;
		uint16_t masterPort = 0;

		std::string login;
		std::string clientHash;

		// SHA-256 hex of server certificate (required for TLS in NetClient).
		std::string serverFingerprintHex;
		bool allowInsecureDev = false;

		uint32_t clients = 0;
		float rampUpSec = 0.0f;
		uint32_t durationSec = 0;

		uint32_t instanceIndex = 0;
		uint32_t instances = 1;

		Scenario scenario = Scenario::Mix;
	};

	class LoadTester final
	{
	public:
		explicit LoadTester(LoadTestConfig cfg);
		~LoadTester();

		// Runs the test and emits a final reporting summary via LOG_*.
		bool Run();

	private:
		struct ClientContext
		{
			NetClient client;
			std::unique_ptr<RequestResponseDispatcher> dispatcher;

			bool connectIssued = false;
			bool connectLatencyRecorded = false;
			std::chrono::steady_clock::time_point connectStart;

			bool authSent = false;
			bool authDone = false;
			bool authSuccess = false;
			std::chrono::steady_clock::time_point authStart;
			uint64_t sessionId = 0;

			// For connect-only we occasionally drain socket events to avoid queue growth.
			std::chrono::steady_clock::time_point lastDrain = {};
		};

		LoadTestConfig m_cfg;
		std::vector<std::unique_ptr<ClientContext>> m_clients;
		std::vector<double> m_connectLatMs;
		std::vector<double> m_authLatMs;

		size_t EffectiveClientCount() const;
		bool Validate() const;

		void SetupClient(NetClient& c) const;
		static Scenario ParseScenario(std::string_view v, bool& ok);
	};
}

