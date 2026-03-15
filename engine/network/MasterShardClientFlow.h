#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::network
{
	class NetClient;
	class RequestResponseDispatcher;
}

namespace engine::network
{
	/// Result of the full client flow (M22.6). Optional shard endpoint and account_id when successful.
	struct MasterShardFlowResult
	{
		bool success = false;
		std::string errorMessage;
		uint64_t account_id = 0;
		uint32_t shard_id = 0;
	};

	/// Orchestrates the vertical slice: connect Master → AUTH → SERVER_LIST → REQUEST_SHARD_TICKET → connect Shard → PRESENT_SHARD_TICKET.
	/// Uses provided NetClient for Master and a second connection for Shard. Ticket is not retained after use.
	/// Timeout per request (default 5s); one retry on timeout (no flood).
	class MasterShardClientFlow
	{
	public:
		MasterShardClientFlow() = default;
		~MasterShardClientFlow() = default;

		/// Set Master address (host, port). Call before Run.
		void SetMasterAddress(std::string host, uint16_t port);
		/// Set credentials for AUTH (login, client_hash per protocol).
		void SetCredentials(std::string login, std::string client_hash);
		/// Request timeout in ms (default 5000). One retry on timeout.
		void SetTimeoutMs(uint32_t ms) { m_timeoutMs = ms; }

		/// Run the full flow. Uses \a masterClient for Master; creates a temporary client for Shard. Returns result with success/error and optional account_id/shard_id.
		/// \a masterClient must be disconnected; it will be connected to Master. Caller keeps ownership of masterClient.
		MasterShardFlowResult Run(NetClient* masterClient);

	private:
		std::string m_masterHost;
		uint16_t m_masterPort = 0;
		std::string m_login;
		std::string m_clientHash;
		uint32_t m_timeoutMs = 5000u;
	};
}
