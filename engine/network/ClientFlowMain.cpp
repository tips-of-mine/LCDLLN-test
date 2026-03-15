/// M22.6 — Headless client that runs the full flow: AUTH → SERVER_LIST → REQUEST_SHARD_TICKET → CONNECT SHARD (ticket). Windows only (NetClient).

#if defined(_WIN32) || defined(WIN32)

#include "engine/network/MasterShardClientFlow.h"
#include "engine/network/NetClient.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

#include <iostream>
#include <cstdlib>

int main(int argc, char** argv)
{
	engine::core::Config config = engine::core::Config::Load("config.json", argc, argv);
	engine::core::LogSettings logSettings;
	logSettings.level = engine::core::LogLevel::Info;
	logSettings.console = true;
	logSettings.flushAlways = true;
	engine::core::Log::Init(logSettings);

	std::string masterHost = config.GetString("client.master_host", "localhost");
	uint16_t masterPort = static_cast<uint16_t>(config.GetInt("client.master_port", 3840));
	std::string login = config.GetString("client.login", "testuser");
	std::string clientHash = config.GetString("client.client_hash", "");

	engine::network::NetClient masterClient;
	masterClient.SetAllowInsecureDev(true);
	engine::network::MasterShardClientFlow flow;
	flow.SetMasterAddress(masterHost, masterPort);
	flow.SetCredentials(login, clientHash);
	flow.SetTimeoutMs(5000);

	engine::network::MasterShardFlowResult result = flow.Run(&masterClient);

	if (result.success)
	{
		LOG_INFO(Net, "[ClientFlowMain] Flow OK (shard_id={})", result.shard_id);
		engine::core::Log::Shutdown();
		return 0;
	}
	LOG_WARN(Net, "[ClientFlowMain] Flow failed: {}", result.errorMessage);
	engine::core::Log::Shutdown();
	std::cerr << "[ClientFlowMain] Flow failed: " << result.errorMessage << std::endl;
	return 1;
}

#else
int main(int, char**)
{
	return 0;
}
#endif
