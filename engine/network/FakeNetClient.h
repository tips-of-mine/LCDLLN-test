#pragma once

/// @file FakeNetClient.h
/// @brief Test-only in-memory clone of NetClient for end-to-end flow tests (Phase 3.10.1).
/// @details Same public surface as NetClient (Connect, Disconnect, Send, PollEvents, GetState)
///          but no socket and no thread — packets sent here are surfaced as PacketReceived
///          events on the *peer* FakeNetClient (set via SetPeer). This lets tests wire two
///          fakes back-to-back to simulate a client/server exchange entirely in-process.
/// @note Intentionally not a subclass of NetClient — that class is concrete and refactoring
///       it into an interface would ripple. Instead, MasterShardClientFlow takes a NetClient*
///       and we accept that this fake won't be drop-in there until a later phase. For now
///       this is used to test request/response round-trips at a level above raw sockets.

#include "engine/network/NetClient.h" // NetClientEvent, NetClientEventType, NetClientState

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace engine::network::test
{
	/// In-memory NetClient stand-in. Single-threaded, no socket. Pairs are wired with SetPeer.
	class FakeNetClient
	{
	public:
		FakeNetClient() = default;
		~FakeNetClient() = default;
		FakeNetClient(const FakeNetClient&) = delete;
		FakeNetClient& operator=(const FakeNetClient&) = delete;

		/// Wire two fakes back-to-back. Calling Send on one queues a PacketReceived on the peer.
		/// Setting peer to nullptr disconnects the link.
		void SetPeer(FakeNetClient* peer);

		/// Simulate a successful connect — pushes a Connected event and flips state to Connected.
		/// host/port are stored but ignored (no real socket). Symmetric : the peer also flips to Connected.
		void Connect(const std::string& host, uint16_t port);

		/// Simulate a Disconnect — pushes a Disconnected(reason) event on this client AND on the peer
		/// (mirrors the real-world behaviour where one side closing causes the other to see EOF).
		void Disconnect(std::string reason);

		/// Queue a complete protocol v1 packet for "transmission". Surfaces immediately as a
		/// PacketReceived event on the peer (no framing / partial reads — this is at packet level).
		bool Send(std::span<const uint8_t> packet);

		/// Drain queued events. Same semantics as NetClient::PollEvents (FIFO move).
		std::vector<NetClientEvent> PollEvents();

		NetClientState GetState() const { return m_state.load(); }

		/// Test helper — push a PacketReceived event manually (e.g. injecting a server response
		/// without going through a peer). Does NOT call the peer.
		void InjectReceivedPacket(std::vector<uint8_t> packet);

	private:
		void PushEvent(NetClientEvent ev);

		std::atomic<NetClientState>     m_state{ NetClientState::Disconnected };
		mutable std::mutex              m_mutex;
		std::vector<NetClientEvent>     m_events;     ///< drained by PollEvents
		FakeNetClient*                  m_peer = nullptr;
		std::string                     m_pendingHost;
		uint16_t                        m_pendingPort = 0;
	};
}
