#pragma once

#include "engine/network/ProtocolV1Constants.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace engine::network
{
	class NetClient;
}

namespace engine::network
{
	/// Shard-side client: outbound persistent TLS to Master, register at startup, periodic heartbeat, reconnect with backoff on disconnect.
	/// Uses NetClient (TLS). Config: master host/port, TLS fingerprint or allow insecure dev, shard name/endpoint/max_capacity/build_version.
	class ShardToMasterClient
	{
	public:
		ShardToMasterClient();
		~ShardToMasterClient();

		/// Set Master address and TLS. Call before Start().
		void SetMasterAddress(std::string host, uint16_t port);
		/// Set server cert fingerprint (SHA-256 hex). If empty and not AllowInsecureDev, TLS is not used.
		void SetExpectedServerFingerprint(std::string hexFingerprint);
		void SetAllowInsecureDev(bool allow);

		/// Shard identity for REGISTER. Call before Start().
		void SetShardIdentity(std::string name, std::string endpoint, uint32_t max_capacity, std::string build_version);

		/// Start connecting. Returns immediately; result via Pump() / GetState().
		void Start();

		/// Call periodically (e.g. every 100 ms). Processes events, sends register on connect, sends heartbeat on interval, schedules reconnect on disconnect.
		void Pump();

		/// Current load to send in register and heartbeats. Default 0.
		void SetCurrentLoad(uint32_t load) { m_current_load = load; }

		/// Heartbeat interval in seconds. Default 10. Call before Start() to take effect.
		void SetHeartbeatIntervalSec(int sec) { m_heartbeat_interval_sec = (sec > 0) ? sec : 10; }

		enum class State
		{
			Disconnected,
			Connecting,
			Registering,
			Registered,
		};
		State GetState() const { return m_state; }
		uint32_t GetShardId() const { return m_shard_id; }

	private:
		void SendRegister();
		void SendHeartbeat();
		void OnConnected();
		void OnDisconnected(std::string_view reason);
		void OnPacketReceived(const uint8_t* data, size_t size);
		void ScheduleReconnect();

		std::unique_ptr<NetClient> m_client;
		std::string m_host;
		uint16_t m_port = 0;
		std::string m_fingerprint;
		bool m_allow_insecure = false;
		std::string m_name;
		std::string m_endpoint;
		uint32_t m_max_capacity = 0;
		std::string m_build_version;
		uint32_t m_current_load = 0;

		State m_state = State::Disconnected;
		uint32_t m_shard_id = 0;
		std::chrono::steady_clock::time_point m_last_heartbeat_sent{};
		std::chrono::steady_clock::time_point m_reconnect_after{};
		int m_reconnect_backoff_sec = 1;
		int m_heartbeat_interval_sec = 10;
	};
}
