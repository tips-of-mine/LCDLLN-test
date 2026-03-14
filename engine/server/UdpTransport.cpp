#include "engine/server/UdpTransport.h"

#include "engine/core/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstdio>

namespace engine::server
{
	namespace
	{
		/// Return the native socket value stored inside the transport.
		SOCKET ToNativeSocket(uintptr_t socketHandle)
		{
			return static_cast<SOCKET>(socketHandle);
		}

		/// Return true when the socket error only means "no datagram available".
		bool IsWouldBlockError(int errorCode)
		{
			return errorCode == WSAEWOULDBLOCK;
		}
	}

	UdpTransport::~UdpTransport()
	{
		Shutdown();
	}

	bool UdpTransport::Init(uint16_t listenPort)
	{
		if (IsValid())
		{
			LOG_WARN(Net, "[UdpTransport] Init ignored: already active on port {}", m_listenPort);
			return true;
		}

		WSADATA wsaData{};
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			LOG_ERROR(Net, "[UdpTransport] Init FAILED: WSAStartup failed");
			return false;
		}
		m_wsaStarted = true;

		SOCKET socketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socketHandle == INVALID_SOCKET)
		{
			LOG_ERROR(Net, "[UdpTransport] Init FAILED: socket() failed (wsa={})", WSAGetLastError());
			Shutdown();
			return false;
		}

		u_long nonBlocking = 1;
		if (ioctlsocket(socketHandle, FIONBIO, &nonBlocking) != 0)
		{
			LOG_ERROR(Net, "[UdpTransport] Init FAILED: ioctlsocket(FIONBIO) failed (wsa={})", WSAGetLastError());
			closesocket(socketHandle);
			Shutdown();
			return false;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(listenPort);
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
		{
			LOG_ERROR(Net, "[UdpTransport] Init FAILED: bind(port={}) failed (wsa={})", listenPort, WSAGetLastError());
			closesocket(socketHandle);
			Shutdown();
			return false;
		}

		m_socketHandle = static_cast<uintptr_t>(socketHandle);
		m_listenPort = listenPort;
		m_receivedPackets = 0;
		m_sentPackets = 0;
		LOG_INFO(Net, "[UdpTransport] Init OK (port={}, non_blocking=true)", m_listenPort);
		return true;
	}

	void UdpTransport::Shutdown()
	{
		if (m_socketHandle != 0)
		{
			closesocket(ToNativeSocket(m_socketHandle));
			m_socketHandle = 0;
		}

		if (m_wsaStarted)
		{
			WSACleanup();
			m_wsaStarted = false;
		}

		if (m_listenPort != 0 || m_receivedPackets != 0 || m_sentPackets != 0)
		{
			LOG_INFO(Net, "[UdpTransport] Destroyed (port={}, rx_packets={}, tx_packets={})",
				m_listenPort, m_receivedPackets, m_sentPackets);
		}

		m_listenPort = 0;
		m_receivedPackets = 0;
		m_sentPackets = 0;
	}

	size_t UdpTransport::Receive(std::vector<Datagram>& outDatagrams, size_t maxDatagrams)
	{
		if (!IsValid())
		{
			LOG_WARN(Net, "[UdpTransport] Receive ignored: transport not initialized");
			return 0;
		}

		outDatagrams.clear();
		outDatagrams.reserve(std::max(outDatagrams.capacity(), maxDatagrams));
		for (size_t i = 0; i < maxDatagrams; ++i)
		{
			Datagram datagram{};
			sockaddr_in from{};
			int fromLength = sizeof(from);
			const int received = recvfrom(
				ToNativeSocket(m_socketHandle),
				reinterpret_cast<char*>(datagram.bytes.data()),
				static_cast<int>(datagram.bytes.size()),
				0,
				reinterpret_cast<sockaddr*>(&from),
				&fromLength);
			if (received == SOCKET_ERROR)
			{
				const int errorCode = WSAGetLastError();
				if (IsWouldBlockError(errorCode))
				{
					break;
				}

				LOG_WARN(Net, "[UdpTransport] Receive failed (wsa={})", errorCode);
				break;
			}

			if (received <= 0)
			{
				continue;
			}

			datagram.endpoint.address = ntohl(from.sin_addr.s_addr);
			datagram.endpoint.port = ntohs(from.sin_port);
			datagram.size = static_cast<size_t>(received);
			outDatagrams.push_back(datagram);
			++m_receivedPackets;
		}

		if (!outDatagrams.empty())
		{
			LOG_DEBUG(Net, "[UdpTransport] Received {} datagram(s)", outDatagrams.size());
		}
		return outDatagrams.size();
	}

	bool UdpTransport::Send(const Endpoint& endpoint, std::span<const std::byte> bytes)
	{
		if (!IsValid())
		{
			LOG_WARN(Net, "[UdpTransport] Send FAILED: transport not initialized");
			return false;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(endpoint.port);
		address.sin_addr.s_addr = htonl(endpoint.address);
		const int sent = sendto(
			ToNativeSocket(m_socketHandle),
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<int>(bytes.size()),
			0,
			reinterpret_cast<const sockaddr*>(&address),
			sizeof(address));
		if (sent == SOCKET_ERROR || static_cast<size_t>(sent) != bytes.size())
		{
			LOG_WARN(Net, "[UdpTransport] Send FAILED (endpoint={}, bytes={}, wsa={})",
				EndpointToString(endpoint), bytes.size(), WSAGetLastError());
			return false;
		}

		++m_sentPackets;
		return true;
	}

	std::string UdpTransport::EndpointToString(const Endpoint& endpoint)
	{
		const uint32_t a = endpoint.address;
		char buffer[32]{};
		std::snprintf(
			buffer,
			sizeof(buffer),
			"%u.%u.%u.%u:%u",
			static_cast<unsigned>((a >> 24) & 0xFFu),
			static_cast<unsigned>((a >> 16) & 0xFFu),
			static_cast<unsigned>((a >> 8) & 0xFFu),
			static_cast<unsigned>(a & 0xFFu),
			static_cast<unsigned>(endpoint.port));
		return buffer;
	}
}
