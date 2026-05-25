#include "src/shardd/world/UdpTransport.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cstdio>

#if defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <fcntl.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <unistd.h>

#	include <cerrno>
#endif

namespace engine::server
{
	namespace
	{
		// Alias plateforme : sous Windows une SOCKET est un handle opaque, sous POSIX
		// un descripteur de fichier entier. Les longueurs/retours des appels socket
		// different aussi (int sous Win, size_t/ssize_t/socklen_t sous POSIX).
#if defined(_WIN32)
		using NativeSocket = SOCKET;
		using SockLen = int;
		using IoLength = int;
		using IoResult = int;
		constexpr NativeSocket kInvalidNativeSocket = INVALID_SOCKET;
#else
		using NativeSocket = int;
		using SockLen = socklen_t;
		using IoLength = size_t;
		using IoResult = ssize_t;
		constexpr NativeSocket kInvalidNativeSocket = -1;
#endif

		/// Reinterprete le handle opaque stocke en socket native de la plateforme.
		NativeSocket ToNativeSocket(uintptr_t socketHandle)
		{
			return static_cast<NativeSocket>(socketHandle);
		}

		/// Ferme une socket native (closesocket sous Windows, ::close sous POSIX).
		void CloseNativeSocket(NativeSocket socketHandle)
		{
#if defined(_WIN32)
			closesocket(socketHandle);
#else
			::close(socketHandle);
#endif
		}

		/// Passe la socket en mode non bloquant. Renvoie true en cas de succes.
		bool SetNonBlocking(NativeSocket socketHandle)
		{
#if defined(_WIN32)
			u_long nonBlocking = 1;
			return ioctlsocket(socketHandle, FIONBIO, &nonBlocking) == 0;
#else
			const int flags = fcntl(socketHandle, F_GETFL, 0);
			if (flags == -1)
			{
				return false;
			}
			return fcntl(socketHandle, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
		}

		/// Dernier code d'erreur socket de la plateforme (pour les logs).
		int LastSocketError()
		{
#if defined(_WIN32)
			return WSAGetLastError();
#else
			return errno;
#endif
		}

		/// True quand l'erreur signifie simplement "aucun datagramme disponible"
		/// (WSAEWOULDBLOCK sous Windows ; EWOULDBLOCK/EAGAIN sous POSIX).
		bool IsWouldBlockError(int errorCode)
		{
#if defined(_WIN32)
			return errorCode == WSAEWOULDBLOCK;
#else
			return errorCode == EWOULDBLOCK || errorCode == EAGAIN;
#endif
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

#if defined(_WIN32)
		WSADATA wsaData{};
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			LOG_ERROR(Net, "[UdpTransport] Init FAILED: WSAStartup failed");
			return false;
		}
		m_wsaStarted = true;
#endif

		const NativeSocket socketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (socketHandle == kInvalidNativeSocket)
		{
			LOG_ERROR(Net, "[UdpTransport] Init FAILED: socket() failed (err={})", LastSocketError());
			Shutdown();
			return false;
		}

		if (!SetNonBlocking(socketHandle))
		{
			LOG_ERROR(Net, "[UdpTransport] Init FAILED: set non-blocking failed (err={})", LastSocketError());
			CloseNativeSocket(socketHandle);
			Shutdown();
			return false;
		}

		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(listenPort);
		address.sin_addr.s_addr = htonl(INADDR_ANY);
		if (bind(socketHandle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
		{
			LOG_ERROR(Net, "[UdpTransport] Init FAILED: bind(port={}) failed (err={})", listenPort, LastSocketError());
			CloseNativeSocket(socketHandle);
			Shutdown();
			return false;
		}

		// Resout le port effectivement assigne : indispensable quand listenPort == 0
		// (port ephemere choisi par l'OS), sinon renvoie le port demande.
		uint16_t boundPort = listenPort;
		sockaddr_in boundAddress{};
		SockLen boundLength = static_cast<SockLen>(sizeof(boundAddress));
		if (getsockname(socketHandle, reinterpret_cast<sockaddr*>(&boundAddress), &boundLength) == 0)
		{
			boundPort = ntohs(boundAddress.sin_port);
		}

		m_socketHandle = static_cast<uintptr_t>(socketHandle);
		m_socketOpen = true;
		m_listenPort = boundPort;
		m_receivedPackets = 0;
		m_sentPackets = 0;
		LOG_INFO(Net, "[UdpTransport] Init OK (port={}, non_blocking=true)", m_listenPort);
		return true;
	}

	void UdpTransport::Shutdown()
	{
		if (m_socketOpen)
		{
			CloseNativeSocket(ToNativeSocket(m_socketHandle));
			m_socketHandle = 0;
			m_socketOpen = false;
		}

#if defined(_WIN32)
		if (m_wsaStarted)
		{
			WSACleanup();
			m_wsaStarted = false;
		}
#endif

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
		outDatagrams.reserve((std::max)(outDatagrams.capacity(), maxDatagrams));
		for (size_t i = 0; i < maxDatagrams; ++i)
		{
			Datagram datagram{};
			sockaddr_in from{};
			SockLen fromLength = static_cast<SockLen>(sizeof(from));
			const IoResult received = recvfrom(
				ToNativeSocket(m_socketHandle),
				reinterpret_cast<char*>(datagram.bytes.data()),
				static_cast<IoLength>(datagram.bytes.size()),
				0,
				reinterpret_cast<sockaddr*>(&from),
				&fromLength);
			if (received < 0)
			{
				const int errorCode = LastSocketError();
				if (IsWouldBlockError(errorCode))
				{
					break;
				}

				LOG_WARN(Net, "[UdpTransport] Receive failed (err={})", errorCode);
				break;
			}

			if (received == 0)
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
		const IoResult sent = sendto(
			ToNativeSocket(m_socketHandle),
			reinterpret_cast<const char*>(bytes.data()),
			static_cast<IoLength>(bytes.size()),
			0,
			reinterpret_cast<const sockaddr*>(&address),
			sizeof(address));
		if (sent < 0 || static_cast<size_t>(sent) != bytes.size())
		{
			LOG_WARN(Net, "[UdpTransport] Send FAILED (endpoint={}, bytes={}, err={})",
				EndpointToString(endpoint), bytes.size(), LastSocketError());
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
