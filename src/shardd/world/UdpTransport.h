#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace engine::server
{
	/// Compact UDP endpoint representation used by the server skeleton.
	struct Endpoint
	{
		uint32_t address = 0;
		uint16_t port = 0;

		/// Compare two endpoints for exact address and port equality.
		bool operator==(const Endpoint& other) const = default;
	};

	/// Received datagram storage used during the server tick.
	struct Datagram
	{
		Endpoint endpoint{};
		std::array<std::byte, 512> bytes{};
		size_t size = 0;
	};

	/// Minimal non-blocking UDP transport for the Windows headless server.
	class UdpTransport final
	{
	public:
		/// Construct an empty transport that owns no socket yet.
		UdpTransport() = default;

		/// Close the socket and release Winsock state if still active.
		~UdpTransport();

		/// Open and bind a UDP socket on the requested listen port.
		bool Init(uint16_t listenPort);

		/// Close the UDP socket and release transport resources.
		void Shutdown();

		/// Receive up to `maxDatagrams` packets without blocking.
		size_t Receive(std::vector<Datagram>& outDatagrams, size_t maxDatagrams);

		/// Send one datagram to the provided endpoint.
		bool Send(const Endpoint& endpoint, std::span<const std::byte> bytes);

		/// Convert an endpoint to a dotted decimal string for logs.
		static std::string EndpointToString(const Endpoint& endpoint);

		/// Return the total number of packets received since init.
		uint64_t ReceivedPacketCount() const { return m_receivedPackets; }

		/// Return the total number of packets sent since init.
		uint64_t SentPacketCount() const { return m_sentPackets; }

		/// Return true when the socket is ready for send/receive.
		bool IsValid() const { return m_socketHandle != 0; }

	private:
		uintptr_t m_socketHandle = 0;
		bool m_wsaStarted = false;
		uint16_t m_listenPort = 0;
		uint64_t m_receivedPackets = 0;
		uint64_t m_sentPackets = 0;
	};
}
