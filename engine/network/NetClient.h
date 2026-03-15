#pragma once

#include "engine/network/ProtocolV1Constants.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace engine::network
{
	/// Connection state for the TCP client state machine.
	enum class NetClientState
	{
		Disconnected,
		Connecting,
		Connected,
		Disconnecting
	};

	/// Event type delivered to the main thread (do not run user callbacks in network thread).
	enum class NetClientEventType
	{
		Connected,
		Disconnected,
		PacketReceived
	};

	/// One event pushed from the network thread; consume via PollEvents().
	struct NetClientEvent
	{
		NetClientEventType type = NetClientEventType::Disconnected;
		/// Reason string for Disconnected (error message or voluntary disconnect).
		std::string reason;
		/// Full packet bytes (header + payload) for PacketReceived; valid protocol v1 packet.
		std::vector<uint8_t> packet;
	};

	/// Non-blocking TCP client with dedicated network thread, protocol v1 framing, and event queue.
	/// Connect has a 10s timeout; RX uses partial reads and PacketView framing; TX uses a non-blocking write queue.
	/// Counters: bytes in/out, packets in/out. Do not invoke user callbacks from the network thread.
	class NetClient
	{
	public:
		NetClient();
		~NetClient();

		/// Start connecting to host:port. Returns immediately; result via PollEvents(). Timeout 10s.
		void Connect(const std::string& host, uint16_t port);

		/// Request disconnect. A Disconnected event will be pushed with the given reason.
		void Disconnect(std::string reason);

		/// Enqueue packet to send (non-blocking). Packet is copied. Returns false if not connected or queue full.
		bool Send(std::span<const uint8_t> packet);

		/// Poll events from the network thread. Call from main thread only. Returns and clears the event queue.
		std::vector<NetClientEvent> PollEvents();

		/// Current state (updated by network thread).
		NetClientState GetState() const;

		/// Counters (thread-safe).
		uint64_t GetBytesIn() const;
		uint64_t GetBytesOut() const;
		uint64_t GetPacketsIn() const;
		uint64_t GetPacketsOut() const;

	private:
		void NetworkThreadRun();

		std::atomic<NetClientState> m_state{ NetClientState::Disconnected };
		std::atomic<uint64_t> m_bytesIn{ 0 };
		std::atomic<uint64_t> m_bytesOut{ 0 };
		std::atomic<uint64_t> m_packetsIn{ 0 };
		std::atomic<uint64_t> m_packetsOut{ 0 };

		std::mutex m_mutex;
		std::string m_pendingHost;
		uint16_t m_pendingPort = 0;
		bool m_pendingConnect = false;
		bool m_requestDisconnect = false;
		std::string m_disconnectReason;

		std::vector<std::vector<uint8_t>> m_writeQueue;
		std::vector<NetClientEvent> m_eventQueue;

		std::thread m_networkThread;
		std::atomic<bool> m_quit{ false };
	};
}
