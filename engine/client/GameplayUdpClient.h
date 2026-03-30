#pragma once

#include "engine/server/ServerProtocol.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::client
{
	/// Minimal non-blocking UDP client for authoritative gameplay packets (Windows / M35.2).
	/// Connects to `host:port`, sends encoded \ref engine::server protocol datagrams, receives replies.
	class GameplayUdpClient final
	{
	public:
		GameplayUdpClient() = default;
		GameplayUdpClient(const GameplayUdpClient&) = delete;
		GameplayUdpClient& operator=(const GameplayUdpClient&) = delete;

		~GameplayUdpClient();

		/// Bind local socket and `connect()` to the gameplay server. Logs and returns false on failure.
		bool Init(std::string host, uint16_t port);

		/// Close socket and reset state. Safe to call multiple times.
		void Shutdown();

		/// Server-assigned id after \ref DecodeWelcome (0 before handshake completes).
		uint32_t ServerClientId() const { return m_serverClientId; }

		bool IsActive() const { return m_active; }

		/// Send one Hello datagram (character / persistence nonce).
		bool SendHello(uint16_t requestedTickHz, uint16_t requestedSnapshotHz, uint32_t clientNonce);

		/// Send TalkRequest (e.g. `vendor:1` to open shop).
		bool SendTalkRequest(uint32_t clientId, std::string_view targetId);

		bool SendShopBuyRequest(uint32_t clientId, uint32_t vendorId, uint32_t itemId, uint32_t quantity);

		bool SendShopSellRequest(uint32_t clientId, uint32_t vendorId, uint32_t itemId, uint32_t quantity);

		bool SendAuctionBrowseRequest(const engine::server::AuctionBrowseRequestMessage& message);

		bool SendAuctionListItemRequest(const engine::server::AuctionListItemRequestMessage& message);

		bool SendAuctionBidRequest(const engine::server::AuctionBidRequestMessage& message);

		bool SendAuctionBuyoutRequest(const engine::server::AuctionBuyoutRequestMessage& message);

		/// Non-blocking recv loop; each full datagram becomes one element. Welcome updates \ref ServerClientId.
		std::vector<std::vector<std::byte>> PollIncoming();

	private:
		bool EnsureWinsock();
		bool SendBytes(std::span<const std::byte> packet);
		void HandleMaybeWelcome(std::span<const std::byte> packet);

		void* m_socket = nullptr; // SOCKET
		bool m_winsockStarted = false;
		bool m_active = false;
		uint32_t m_serverClientId = 0;
		std::string m_host;
		uint16_t m_port = 0;
	};
}
