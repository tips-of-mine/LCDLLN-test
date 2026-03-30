#include "engine/client/GameplayUdpClient.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <array>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <winsock2.h>
#	include <ws2tcpip.h>
#endif

namespace engine::client
{
	namespace
	{
#if defined(_WIN32)
		SOCKET Native(void* s) { return static_cast<SOCKET>(reinterpret_cast<uintptr_t>(s)); }

		void* ToOpaque(SOCKET s) { return reinterpret_cast<void*>(static_cast<uintptr_t>(s)); }

		bool SetNonBlocking(SOCKET s)
		{
			u_long mode = 1;
			return ioctlsocket(s, FIONBIO, &mode) == 0;
		}
#endif
	}

	GameplayUdpClient::~GameplayUdpClient()
	{
		Shutdown();
	}

	bool GameplayUdpClient::EnsureWinsock()
	{
#if defined(_WIN32)
		if (m_winsockStarted)
		{
			return true;
		}
		WSADATA wsa{};
		const int r = WSAStartup(MAKEWORD(2, 2), &wsa);
		if (r != 0)
		{
			LOG_ERROR(Net, "[GameplayUdpClient] WSAStartup FAILED (code={})", r);
			return false;
		}
		m_winsockStarted = true;
		LOG_INFO(Net, "[GameplayUdpClient] WSAStartup OK");
		return true;
#else
		LOG_WARN(Net, "[GameplayUdpClient] Init ignored: UDP gameplay client is Windows-only in this build");
		return false;
#endif
	}

	bool GameplayUdpClient::Init(std::string host, uint16_t port)
	{
		if (m_active)
		{
			LOG_WARN(Net, "[GameplayUdpClient] Init ignored: already active");
			return true;
		}

		if (!EnsureWinsock())
		{
			return false;
		}

#if defined(_WIN32)
		addrinfo hints{};
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		hints.ai_protocol = IPPROTO_UDP;

		addrinfo* resolved = nullptr;
		const std::string portStr = std::to_string(static_cast<int>(port));
		const int gai = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &resolved);
		if (gai != 0 || resolved == nullptr)
		{
			LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: getaddrinfo ({}, {})", host, portStr);
			if (resolved)
				freeaddrinfo(resolved);
			return false;
		}

		SOCKET s = socket(resolved->ai_family, resolved->ai_socktype, resolved->ai_protocol);
		if (s == INVALID_SOCKET)
		{
			LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: socket() wsa={}", WSAGetLastError());
			freeaddrinfo(resolved);
			return false;
		}

		if (::connect(s, resolved->ai_addr, static_cast<int>(resolved->ai_addrlen)) == SOCKET_ERROR)
		{
			LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: connect() wsa={}", WSAGetLastError());
			closesocket(s);
			freeaddrinfo(resolved);
			return false;
		}
		freeaddrinfo(resolved);

		if (!SetNonBlocking(s))
		{
			LOG_ERROR(Net, "[GameplayUdpClient] Init FAILED: non-blocking wsa={}", WSAGetLastError());
			closesocket(s);
			return false;
		}

		m_socket = ToOpaque(s);
		m_host = std::move(host);
		m_port = port;
		m_serverClientId = 0;
		m_active = true;
		LOG_INFO(Net, "[GameplayUdpClient] Init OK (host={}, port={})", m_host, m_port);
		return true;
#else
		(void)host;
		(void)port;
		return false;
#endif
	}

	void GameplayUdpClient::Shutdown()
	{
#if defined(_WIN32)
		if (m_socket != nullptr)
		{
			closesocket(Native(m_socket));
			m_socket = nullptr;
			LOG_INFO(Net, "[GameplayUdpClient] Socket closed (host={})", m_host);
		}
#endif
		// Intentionally no WSACleanup(): Winsock may be shared with other subsystems (e.g. NetClient).
		m_winsockStarted = false;
		m_active = false;
		m_serverClientId = 0;
		m_host.clear();
		m_port = 0;
	}

	bool GameplayUdpClient::SendBytes(std::span<const std::byte> packet)
	{
#if defined(_WIN32)
		if (!m_active || m_socket == nullptr)
		{
			LOG_WARN(Net, "[GameplayUdpClient] Send ignored: not active");
			return false;
		}
		const int r = send(Native(m_socket),
			reinterpret_cast<const char*>(packet.data()),
			static_cast<int>(packet.size()),
			0);
		if (r == SOCKET_ERROR)
		{
			const int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK)
			{
				LOG_WARN(Net, "[GameplayUdpClient] Send would block");
				return false;
			}
			LOG_ERROR(Net, "[GameplayUdpClient] Send FAILED wsa={}", err);
			return false;
		}
		LOG_DEBUG(Net, "[GameplayUdpClient] Sent {} bytes", r);
		return true;
#else
		(void)packet;
		return false;
#endif
	}

	void GameplayUdpClient::HandleMaybeWelcome(std::span<const std::byte> packet)
	{
		engine::server::MessageKind kind{};
		if (!engine::server::PeekMessageKind(packet, kind) || kind != engine::server::MessageKind::Welcome)
		{
			return;
		}
		engine::server::WelcomeMessage welcome{};
		if (!engine::server::DecodeWelcome(packet, welcome))
		{
			LOG_WARN(Net, "[GameplayUdpClient] Welcome decode FAILED");
			return;
		}
		m_serverClientId = welcome.clientId;
		LOG_INFO(Net,
			"[GameplayUdpClient] Welcome received (client_id={}, tick_hz={}, snapshot_hz={})",
			welcome.clientId,
			welcome.tickHz,
			welcome.snapshotHz);
	}

	bool GameplayUdpClient::SendHello(uint16_t requestedTickHz, uint16_t requestedSnapshotHz, uint32_t clientNonce)
	{
		engine::server::HelloMessage hm{};
		hm.requestedTickHz = requestedTickHz;
		hm.requestedSnapshotHz = requestedSnapshotHz;
		hm.clientNonce = clientNonce;
		const std::vector<std::byte> packet = engine::server::EncodeHello(hm);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] Hello sent (nonce={})", clientNonce);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] Hello send FAILED (nonce={})", clientNonce);
		}
		return ok;
	}

	bool GameplayUdpClient::SendTalkRequest(uint32_t clientId, std::string_view targetId)
	{
		engine::server::TalkRequestMessage msg{};
		msg.clientId = clientId;
		msg.targetId = std::string(targetId);
		const std::vector<std::byte> packet = engine::server::EncodeTalkRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net, "[GameplayUdpClient] TalkRequest sent (client_id={}, target={})", clientId, msg.targetId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] TalkRequest FAILED (client_id={})", clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendShopBuyRequest(uint32_t clientId, uint32_t vendorId, uint32_t itemId, uint32_t quantity)
	{
		engine::server::ShopBuyRequestMessage msg{};
		msg.clientId = clientId;
		msg.vendorId = vendorId;
		msg.itemId = itemId;
		msg.quantity = quantity;
		const std::vector<std::byte> packet = engine::server::EncodeShopBuyRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] ShopBuy sent (client_id={}, vendor_id={}, item_id={}, qty={})",
				clientId,
				vendorId,
				itemId,
				quantity);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] ShopBuy FAILED (vendor_id={}, item_id={})", vendorId, itemId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendShopSellRequest(uint32_t clientId, uint32_t vendorId, uint32_t itemId, uint32_t quantity)
	{
		engine::server::ShopSellRequestMessage msg{};
		msg.clientId = clientId;
		msg.vendorId = vendorId;
		msg.itemId = itemId;
		msg.quantity = quantity;
		const std::vector<std::byte> packet = engine::server::EncodeShopSellRequest(msg);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] ShopSell sent (client_id={}, vendor_id={}, item_id={}, qty={})",
				clientId,
				vendorId,
				itemId,
				quantity);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] ShopSell FAILED (vendor_id={}, item_id={})", vendorId, itemId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAuctionBrowseRequest(const engine::server::AuctionBrowseRequestMessage& message)
	{
		const std::vector<std::byte> packet = engine::server::EncodeAuctionBrowseRequest(message);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] AuctionBrowse sent (client_id={}, sort={}, max_rows={})",
				message.clientId,
				message.sortMode,
				message.maxRows);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AuctionBrowse send FAILED (client_id={})", message.clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAuctionListItemRequest(const engine::server::AuctionListItemRequestMessage& message)
	{
		const std::vector<std::byte> packet = engine::server::EncodeAuctionListItemRequest(message);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] AuctionListItem sent (client_id={}, item_id={}, qty={})",
				message.clientId,
				message.itemId,
				message.quantity);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AuctionListItem send FAILED (client_id={})", message.clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAuctionBidRequest(const engine::server::AuctionBidRequestMessage& message)
	{
		const std::vector<std::byte> packet = engine::server::EncodeAuctionBidRequest(message);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] AuctionBid sent (client_id={}, listing_id={}, bid={})",
				message.clientId,
				message.listingId,
				message.bidAmount);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AuctionBid send FAILED (client_id={})", message.clientId);
		}
		return ok;
	}

	bool GameplayUdpClient::SendAuctionBuyoutRequest(const engine::server::AuctionBuyoutRequestMessage& message)
	{
		const std::vector<std::byte> packet = engine::server::EncodeAuctionBuyoutRequest(message);
		const bool ok = SendBytes(packet);
		if (ok)
		{
			LOG_INFO(Net,
				"[GameplayUdpClient] AuctionBuyout sent (client_id={}, listing_id={})",
				message.clientId,
				message.listingId);
		}
		else
		{
			LOG_WARN(Net, "[GameplayUdpClient] AuctionBuyout send FAILED (client_id={})", message.clientId);
		}
		return ok;
	}

	std::vector<std::vector<std::byte>> GameplayUdpClient::PollIncoming()
	{
		std::vector<std::vector<std::byte>> out;
#if defined(_WIN32)
		if (!m_active || m_socket == nullptr)
		{
			return out;
		}

		for (;;)
		{
			std::array<std::byte, 2048> buf{};
			const int r = recv(Native(m_socket), reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
			if (r == SOCKET_ERROR)
			{
				const int err = WSAGetLastError();
				if (err == WSAEWOULDBLOCK)
				{
					break;
				}
				LOG_WARN(Net, "[GameplayUdpClient] Recv error wsa={}", err);
				break;
			}
			if (r <= 0)
			{
				break;
			}

			std::vector<std::byte> one(static_cast<size_t>(r));
			std::copy_n(buf.data(), static_cast<size_t>(r), one.begin());
			HandleMaybeWelcome(one);
			out.push_back(std::move(one));
		}
#endif
		return out;
	}
}
