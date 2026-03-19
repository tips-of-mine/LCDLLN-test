#include "engine/network/NetClient.h"
#include "engine/network/PacketView.h"

#include "engine/core/Log.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/sha.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iomanip>
#include <sstream>

namespace engine::network
{
	namespace
	{
		constexpr int kConnectTimeoutSeconds = 10;
		constexpr size_t kRxBufferCapacity = kProtocolV1MaxPacketSize * 2u;
		constexpr size_t kMaxWriteQueueSize = 1024u;

		SOCKET ToNativeSocket(uintptr_t h) { return static_cast<SOCKET>(h); }
		bool IsWouldBlock(int err) { return err == WSAEWOULDBLOCK; }

		void CloseSocket(uintptr_t& handle)
		{
			if (handle != 0)
			{
				closesocket(ToNativeSocket(handle));
				handle = 0;
			}
		}

		/// Normalize hex fingerprint (lowercase, strip non-hex). Empty if not 64 chars (SHA-256).
		std::string NormalizeFingerprintHex(const std::string& hex)
		{
			std::string out;
			out.reserve(hex.size());
			for (unsigned char c : hex)
			{
				if (std::isxdigit(static_cast<unsigned char>(c)))
					out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			if (out.size() != 64u)
				return {};
			return out;
		}

		/// Compute SHA-256 of X509 cert DER, return as lowercase hex string (64 chars).
		std::string X509FingerprintSha256Hex(X509* cert)
		{
			if (cert == nullptr)
				return {};
			unsigned char* der = nullptr;
			int len = i2d_X509(cert, &der);
			if (len <= 0 || der == nullptr)
				return {};
			unsigned char hash[SHA256_DIGEST_LENGTH];
			SHA256(der, static_cast<size_t>(len), hash);
			OPENSSL_free(der);
			std::ostringstream os;
			for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
				os << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(hash[i]);
			return os.str();
		}

		void LogSslErrors(const char* context)
		{
			unsigned long err;
			while ((err = ERR_get_error()) != 0)
			{
				char buf[256];
				ERR_error_string_n(err, buf, sizeof(buf));
				LOG_ERROR(Net, "[NetClient] {}: {}", context, buf);
			}
		}

		/// select() for read with timeout. Returns true if data available.
		bool TlsWaitRead(SOCKET s, int timeoutSec)
		{
			fd_set rfds;
			FD_ZERO(&rfds);
			FD_SET(s, &rfds);
			timeval tv{};
			tv.tv_sec = timeoutSec;
			tv.tv_usec = 0;
			return select(0, &rfds, nullptr, nullptr, &tv) > 0;
		}

		/// select() for write with timeout. Returns true if writable.
		bool TlsWaitWrite(SOCKET s, int timeoutSec)
		{
			fd_set wfds;
			FD_ZERO(&wfds);
			FD_SET(s, &wfds);
			timeval tv{};
			tv.tv_sec = timeoutSec;
			tv.tv_usec = 0;
			return select(0, nullptr, &wfds, nullptr, &tv) > 0;
		}

		/// SSL_connect loop with WANT_READ/WANT_WRITE handling. Returns true on success.
		bool TlsHandshakeLoop(SSL* ssl, SOCKET s, int timeoutSec)
		{
			for (;;)
			{
				int r = SSL_connect(ssl);
				if (r == 1)
					return true;
				int err = SSL_get_error(ssl, r);
				if (err == SSL_ERROR_WANT_READ)
				{
					if (!TlsWaitRead(s, timeoutSec))
						return false;
				}
				else if (err == SSL_ERROR_WANT_WRITE)
				{
					if (!TlsWaitWrite(s, timeoutSec))
						return false;
				}
				else
				{
					LogSslErrors("TLS handshake");
					return false;
				}
			}
		}

		/// Verify server cert fingerprint. Returns true if OK or allowInsecure accepted. Logs on mismatch.
		bool TlsVerifyFingerprint(SSL* ssl, std::string_view expected, bool allowInsecure)
		{
			X509* cert = SSL_get_peer_certificate(ssl);
			std::string serverFp = X509FingerprintSha256Hex(cert);
			if (cert)
				X509_free(cert);
			if (serverFp != expected && !allowInsecure)
			{
				LOG_ERROR(Net, "[NetClient] TLS pinning: certificate fingerprint mismatch (got {}), connection refused",
					serverFp);
				return false;
			}
			if (serverFp != expected && allowInsecure)
			{
				LOG_WARN(Net, "[NetClient] TLS allow_insecure_dev: accepting unverified fingerprint");
				return true;
			}
			LOG_INFO(Net, "[NetClient] Certificate fingerprint verified");
			return true;
		}
	}

	NetClient::NetClient()
	{
		LOG_INFO(Net, "[NetClient] Created");
	}

	NetClient::~NetClient()
	{
		m_quit.store(true);
		m_requestDisconnect = true;
		if (m_networkThread.joinable())
			m_networkThread.join();
		LOG_INFO(Net, "[NetClient] Destroyed");
	}

	void NetClient::SetExpectedServerFingerprint(std::string hexFingerprint)
	{
		std::lock_guard lock(m_mutex);
		m_expectedServerFingerprintHex = std::move(hexFingerprint);
	}

	void NetClient::SetAllowInsecureDev(bool allow)
	{
		std::lock_guard lock(m_mutex);
		m_allowInsecureDev = allow;
	}

	void NetClient::Connect(const std::string& host, uint16_t port)
	{
		LOG_INFO(Net, "[NetClient] Connect requested (host={} port={})", host, port);
		if (m_state.load() != NetClientState::Disconnected)
		{
			LOG_WARN(Net, "[NetClient] Connect ignored: state is not Disconnected");
			return;
		}
		std::lock_guard lock(m_mutex);
		m_pendingHost = host;
		m_pendingPort = port;
		m_pendingConnect = true;
		m_requestDisconnect = false;
		if (!m_networkThread.joinable())
		{
			m_networkThread = std::thread(&NetClient::NetworkThreadRun, this);
			LOG_DEBUG(Net, "[NetClient] Network thread started");
		}
	}

	void NetClient::Disconnect(std::string reason)
	{
		LOG_INFO(Net, "[NetClient] Disconnect requested (reason='{}')", reason);
		m_requestDisconnect = true;
		{
			std::lock_guard lock(m_mutex);
			m_disconnectReason = std::move(reason);
		}
	}

	bool NetClient::Send(std::span<const uint8_t> packet)
	{
		if (m_state.load() != NetClientState::Connected)
		{
			LOG_WARN(Net, "[NetClient] Send ignored: not connected");
			return false;
		}
		if (packet.size() < kProtocolV1HeaderSize || packet.size() > kProtocolV1MaxPacketSize)
		{
			LOG_WARN(Net, "[NetClient] Send ignored: invalid packet size {}", packet.size());
			return false;
		}
		std::lock_guard lock(m_mutex);
		if (m_writeQueue.size() >= kMaxWriteQueueSize)
		{
			LOG_WARN(Net, "[NetClient] Send FAILED: write queue full");
			return false;
		}
		m_writeQueue.push_back(std::vector<uint8_t>(packet.begin(), packet.end()));
		return true;
	}

	std::vector<NetClientEvent> NetClient::PollEvents()
	{
		std::lock_guard lock(m_mutex);
		std::vector<NetClientEvent> out = std::move(m_eventQueue);
		m_eventQueue.clear();
		return out;
	}

	NetClientState NetClient::GetState() const
	{
		return m_state.load();
	}

	uint64_t NetClient::GetBytesIn() const { return m_bytesIn.load(); }
	uint64_t NetClient::GetBytesOut() const { return m_bytesOut.load(); }
	uint64_t NetClient::GetPacketsIn() const { return m_packetsIn.load(); }
	uint64_t NetClient::GetPacketsOut() const { return m_packetsOut.load(); }

	void NetClient::TlsCleanupAndDisconnect(void* ssl, void* ctx, uintptr_t& socketHandle, std::string_view reason)
	{
		LOG_DEBUG(Net, "[NetClient] TLS cleanup + disconnect (reason='{}')", std::string(reason));
		SSL* s = static_cast<SSL*>(ssl);
		SSL_CTX* c = static_cast<SSL_CTX*>(ctx);
		if (s != nullptr)
		{
			int shut = SSL_shutdown(s);
			if (shut < 0)
				LOG_ERROR(Net, "[NetClient] TLS cleanup: SSL_shutdown error");
			SSL_free(s);
		}
		if (c != nullptr)
			SSL_CTX_free(c);
		CloseSocket(socketHandle);
		m_state.store(NetClientState::Disconnected);
		std::lock_guard lock(m_mutex);
		m_eventQueue.push_back(
			{ NetClientEventType::Disconnected, std::string(reason), {} });
	}

	void NetClient::NetworkThreadRun()
	{
		LOG_DEBUG(Net, "[NetClient] Network thread started");
		WSADATA wsaData{};
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			LOG_ERROR(Net, "[NetClient] Network thread FAILED: WSAStartup failed");
			m_state.store(NetClientState::Disconnected);
			std::lock_guard lock(m_mutex);
			m_eventQueue.push_back({ NetClientEventType::Disconnected, "WSAStartup failed", {} });
			return;
		}
		LOG_DEBUG(Net, "[NetClient] WSAStartup OK");

		uintptr_t socketHandle = 0;
		SSL* ssl = nullptr;
		std::vector<uint8_t> rxBuffer;
		rxBuffer.reserve(kRxBufferCapacity);
		size_t rxConsumed = 0;
		std::vector<uint8_t> currentSend;
		size_t currentSendOffset = 0;
		bool tlsHandshakeDone = false;

		while (!m_quit.load())
		{
			// --- Pending connect (read intent under lock, do I/O outside lock to avoid holding mutex during select) ---
			{
				std::string host;
				uint16_t port = 0;
				bool doConnect = false;
				{
					std::lock_guard lock(m_mutex);
					if (m_pendingConnect && m_state.load() == NetClientState::Disconnected)
					{
						m_pendingConnect = false;
						host = m_pendingHost;
						port = m_pendingPort;
						doConnect = true;
					}
				}
				if (doConnect)
				{
					m_state.store(NetClientState::Connecting);
					LOG_DEBUG(Net, "[NetClient] Attempting TCP connect to {}:{}", host, port);
					LOG_INFO(Net, "[NetClient] Connecting to {}:{}...", host, port);

					addrinfo hints{};
					hints.ai_family = AF_INET;
					hints.ai_socktype = SOCK_STREAM;
					hints.ai_protocol = IPPROTO_TCP;
					addrinfo* result = nullptr;
					std::string portStr = std::to_string(port);
					if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0)
					{
						LOG_ERROR(Net, "[NetClient] Connect FAILED: getaddrinfo (wsa={})", WSAGetLastError());
						m_state.store(NetClientState::Disconnected);
						std::lock_guard lock(m_mutex);
						m_eventQueue.push_back({ NetClientEventType::Disconnected, "getaddrinfo failed", {} });
						continue;
					}

					SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
					if (s == INVALID_SOCKET)
					{
						LOG_ERROR(Net, "[NetClient] Connect FAILED: socket() (wsa={})", WSAGetLastError());
						freeaddrinfo(result);
						m_state.store(NetClientState::Disconnected);
						std::lock_guard lock(m_mutex);
						m_eventQueue.push_back({ NetClientEventType::Disconnected, "socket() failed", {} });
						continue;
					}

					u_long nonBlocking = 1;
					if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0)
					{
						LOG_ERROR(Net, "[NetClient] Connect FAILED: ioctlsocket(FIONBIO) (wsa={})", WSAGetLastError());
						closesocket(s);
						freeaddrinfo(result);
						m_state.store(NetClientState::Disconnected);
						std::lock_guard lock(m_mutex);
						m_eventQueue.push_back({ NetClientEventType::Disconnected, "non-blocking failed", {} });
						continue;
					}

					int connResult = connect(s, result->ai_addr, static_cast<int>(result->ai_addrlen));
					freeaddrinfo(result);
					if (connResult == 0)
					{
						socketHandle = static_cast<uintptr_t>(s);
						m_state.store(NetClientState::Connected);
						LOG_INFO(Net, "[NetClient] TCP connected to {}:{}", host, port);
						rxBuffer.clear();
						rxConsumed = 0;
						bool useTls = false;
						{ std::lock_guard lock(m_mutex); useTls = !m_expectedServerFingerprintHex.empty(); }
						if (useTls)
						{
							LOG_INFO(Net, "[NetClient] TLS handshake started (host={}:{})", host, port);
							SSL_library_init();
							SSL_load_error_strings();
							SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
							if (!ctx)
							{
								LogSslErrors("SSL_CTX_new");
								TlsCleanupAndDisconnect(nullptr, nullptr, socketHandle, "TLS init failed");
								continue;
							}
							SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
							SSL* mySsl = SSL_new(ctx);
							if (!mySsl)
							{
								LogSslErrors("SSL_new");
								TlsCleanupAndDisconnect(nullptr, ctx, socketHandle, "TLS init failed");
								continue;
							}
							SSL_set_fd(mySsl, static_cast<int>(s));
							if (!TlsHandshakeLoop(mySsl, s, kConnectTimeoutSeconds))
							{
								LOG_WARN(Net, "[NetClient] TLS handshake timeout");
								TlsCleanupAndDisconnect(mySsl, ctx, socketHandle, "TLS handshake timeout");
								continue;
							}
							std::string expectedNorm;
							bool allowInsecure = false;
							{
								std::lock_guard lock(m_mutex);
								expectedNorm = NormalizeFingerprintHex(m_expectedServerFingerprintHex);
								allowInsecure = m_allowInsecureDev;
							}
							if (expectedNorm.empty())
							{
								LOG_ERROR(Net, "[NetClient] TLS pinning: expected fingerprint invalid (must be 64 hex chars)");
								TlsCleanupAndDisconnect(mySsl, ctx, socketHandle, "TLS fingerprint config invalid");
								continue;
							}
							if (!TlsVerifyFingerprint(mySsl, expectedNorm, allowInsecure))
							{
								TlsCleanupAndDisconnect(mySsl, ctx, socketHandle, "certificate fingerprint mismatch");
								continue;
							}
							LOG_INFO(Net, "[NetClient] TLS handshake completed OK");
							ssl = mySsl;
							SSL_CTX_free(ctx);
							tlsHandshakeDone = true;
							std::lock_guard lock(m_mutex);
							m_eventQueue.push_back({ NetClientEventType::Connected, "", {} });
						}
						else
						{
							std::lock_guard lock(m_mutex);
							m_eventQueue.push_back({ NetClientEventType::Connected, "", {} });
						}
					}
					else
					{
						int err = WSAGetLastError();
						if (err != WSAEWOULDBLOCK)
						{
							LOG_ERROR(Net, "[NetClient] Connect FAILED: connect() (wsa={})", err);
							closesocket(s);
							m_state.store(NetClientState::Disconnected);
							std::lock_guard lock(m_mutex);
							m_eventQueue.push_back({ NetClientEventType::Disconnected, "connect() failed", {} });
						}
						else
						{
							// Wait for connection with timeout (outside mutex)
							fd_set wfds;
							FD_ZERO(&wfds);
							FD_SET(s, &wfds);
							timeval tv{};
							tv.tv_sec = kConnectTimeoutSeconds;
							tv.tv_usec = 0;
							int sel = select(0, nullptr, &wfds, nullptr, &tv);
							if (sel > 0)
							{
								int errOpt = 0;
								int errLen = sizeof(errOpt);
								if (getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&errOpt), &errLen) == 0 && errOpt == 0)
								{
									socketHandle = static_cast<uintptr_t>(s);
									m_state.store(NetClientState::Connected);
							LOG_INFO(Net, "[NetClient] TCP connected to {}:{} (async)", host, port);
									rxBuffer.clear();
									rxConsumed = 0;
									bool useTlsAsync = false;
									{ std::lock_guard lock(m_mutex); useTlsAsync = !m_expectedServerFingerprintHex.empty(); }
									if (useTlsAsync)
									{
										LOG_INFO(Net, "[NetClient] TLS handshake started (host={}:{})", host, port);
										SSL_library_init();
										SSL_load_error_strings();
										SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
										if (!ctx)
										{
											LogSslErrors("SSL_CTX_new");
											TlsCleanupAndDisconnect(nullptr, nullptr, socketHandle, "TLS init failed");
											continue;
										}
										SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
										SSL* mySsl = SSL_new(ctx);
										if (!mySsl)
										{
											LogSslErrors("SSL_new");
											TlsCleanupAndDisconnect(nullptr, ctx, socketHandle, "TLS init failed");
											continue;
										}
										SSL_set_fd(mySsl, static_cast<int>(s));
										if (!TlsHandshakeLoop(mySsl, s, kConnectTimeoutSeconds))
										{
											LOG_WARN(Net, "[NetClient] TLS handshake timeout");
											TlsCleanupAndDisconnect(mySsl, ctx, socketHandle, "TLS handshake timeout");
											continue;
										}
										std::string expectedNorm;
										bool allowInsecure = false;
										{
											std::lock_guard lock(m_mutex);
											expectedNorm = NormalizeFingerprintHex(m_expectedServerFingerprintHex);
											allowInsecure = m_allowInsecureDev;
										}
										if (expectedNorm.empty())
										{
											LOG_ERROR(Net, "[NetClient] TLS pinning: expected fingerprint invalid (must be 64 hex chars)");
											TlsCleanupAndDisconnect(mySsl, ctx, socketHandle, "TLS fingerprint config invalid");
											continue;
										}
										if (!TlsVerifyFingerprint(mySsl, expectedNorm, allowInsecure))
										{
											TlsCleanupAndDisconnect(mySsl, ctx, socketHandle, "certificate fingerprint mismatch");
											continue;
										}
										LOG_INFO(Net, "[NetClient] TLS handshake completed OK");
										ssl = mySsl;
										SSL_CTX_free(ctx);
										tlsHandshakeDone = true;
										std::lock_guard lock(m_mutex);
										m_eventQueue.push_back({ NetClientEventType::Connected, "", {} });
									}
									else
									{
										std::lock_guard lock(m_mutex);
										m_eventQueue.push_back({ NetClientEventType::Connected, "", {} });
									}
								}
								else
								{
									LOG_ERROR(Net, "[NetClient] Connect FAILED: connection failed (so_error={})", errOpt);
									closesocket(s);
									m_state.store(NetClientState::Disconnected);
									std::lock_guard lock(m_mutex);
									m_eventQueue.push_back({ NetClientEventType::Disconnected, "connection failed", {} });
								}
							}
							else
							{
								LOG_WARN(Net, "[NetClient] Connect FAILED: timeout ({}s)", kConnectTimeoutSeconds);
								closesocket(s);
								m_state.store(NetClientState::Disconnected);
								std::lock_guard lock(m_mutex);
								m_eventQueue.push_back({ NetClientEventType::Disconnected, "connect timeout", {} });
							}
						}
					}
				}
			}

			// --- Requested disconnect ---
			if (m_requestDisconnect && socketHandle != 0)
			{
				std::string reason;
				{
					std::lock_guard lock(m_mutex);
					reason = m_disconnectReason;
				}
			LOG_DEBUG(Net, "[NetClient] Requested disconnect (reason='{}')", reason.empty() ? "user request" : reason);
				if (ssl != nullptr) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; tlsHandshakeDone = false; }
				CloseSocket(socketHandle);
				m_state.store(NetClientState::Disconnected);
				LOG_INFO(Net, "[NetClient] Disconnected: {}", reason.empty() ? "requested" : reason);
				std::lock_guard lock(m_mutex);
				m_eventQueue.push_back({ NetClientEventType::Disconnected, std::move(reason), {} });
				m_requestDisconnect = false;
				rxBuffer.clear();
				rxConsumed = 0;
				continue;
			}

			// --- Read (partial reads + framing) ---
			if (socketHandle != 0 && m_state.load() == NetClientState::Connected)
			{
				// Compact consumed data
				if (rxConsumed > 0)
				{
					if (rxConsumed >= rxBuffer.size())
						rxBuffer.clear();
					else
					{
						std::memmove(rxBuffer.data(), rxBuffer.data() + rxConsumed, rxBuffer.size() - rxConsumed);
						rxBuffer.resize(rxBuffer.size() - rxConsumed);
					}
					rxConsumed = 0;
				}

				char tmp[4096];
				int received = (ssl != nullptr)
					? SSL_read(ssl, tmp, sizeof(tmp))
					: static_cast<int>(recv(ToNativeSocket(socketHandle), tmp, sizeof(tmp), 0));
				if (received > 0)
				{
					m_bytesIn.fetch_add(static_cast<uint64_t>(received));
					size_t prevSize = rxBuffer.size();
					rxBuffer.resize(prevSize + static_cast<size_t>(received));
					std::memcpy(rxBuffer.data() + prevSize, tmp, static_cast<size_t>(received));

					// Parse complete packets with PacketView
					while (rxBuffer.size() >= kProtocolV1HeaderSize)
					{
						PacketView view;
						PacketParseResult res = PacketView::Parse(rxBuffer.data(), rxBuffer.size(), view);
						if (res == PacketParseResult::Incomplete)
							break;
						if (res == PacketParseResult::Invalid)
						{
							LOG_WARN(Net, "[NetClient] RX invalid packet (size in header or OOB), disconnecting");
							m_requestDisconnect = true;
							{
								std::lock_guard lock(m_mutex);
								m_disconnectReason = "invalid packet";
							}
							break;
						}
						// Ok: deliver full packet
						size_t packetSize = view.Size();
						m_packetsIn.fetch_add(1);
						std::vector<uint8_t> packet(rxBuffer.begin(), rxBuffer.begin() + packetSize);
						{
							std::lock_guard lock(m_mutex);
							m_eventQueue.push_back({ NetClientEventType::PacketReceived, "", std::move(packet) });
						}
						rxConsumed += packetSize;
					}
				}
				else if (received == 0)
				{
					LOG_INFO(Net, "[NetClient] Peer closed connection");
					m_requestDisconnect = true;
					std::lock_guard lock(m_mutex);
					m_disconnectReason = "peer closed";
				}
				else
				{
					if (ssl != nullptr)
					{
						int err = SSL_get_error(ssl, received);
						if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
						{
							LogSslErrors("SSL_read");
							m_requestDisconnect = true;
							{ std::lock_guard lock(m_mutex); m_disconnectReason = "SSL read error"; }
						}
					}
					else
					{
						int err = WSAGetLastError();
						if (!IsWouldBlock(err))
						{
							LOG_WARN(Net, "[NetClient] recv failed (wsa={}), disconnecting", err);
							m_requestDisconnect = true;
							{ std::lock_guard lock(m_mutex); m_disconnectReason = "recv error"; }
						}
					}
				}
			}

			// --- Write (flush queue non-blocking; one packet at a time, partial send retried next loop) ---
			if (socketHandle != 0 && m_state.load() == NetClientState::Connected)
			{
				if (currentSend.empty() && currentSendOffset == 0)
				{
					std::lock_guard lock(m_mutex);
					if (!m_writeQueue.empty())
					{
						currentSend = std::move(m_writeQueue.front());
						m_writeQueue.erase(m_writeQueue.begin());
						currentSendOffset = 0;
					}
				}
				if (!currentSend.empty())
				{
					const uint8_t* ptr = currentSend.data() + currentSendOffset;
					size_t remaining = currentSend.size() - currentSendOffset;
					int sent = (ssl != nullptr)
						? SSL_write(ssl, ptr, static_cast<int>(remaining))
						: static_cast<int>(send(ToNativeSocket(socketHandle),
							reinterpret_cast<const char*>(ptr), static_cast<int>(remaining), 0));
					if (sent > 0)
					{
						m_bytesOut.fetch_add(static_cast<uint64_t>(sent));
						currentSendOffset += static_cast<size_t>(sent);
						if (currentSendOffset >= currentSend.size())
						{
							m_packetsOut.fetch_add(1);
							currentSend.clear();
							currentSendOffset = 0;
						}
					}
					else
					{
						if (ssl != nullptr)
						{
							int err = SSL_get_error(ssl, sent);
							if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
								LogSslErrors("SSL_write");
						}
						else if (!IsWouldBlock(WSAGetLastError()))
							LOG_WARN(Net, "[NetClient] send failed (wsa={})", WSAGetLastError());
					}
				}
			}
			else
			{
				currentSend.clear();
				currentSendOffset = 0;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}

		LOG_DEBUG(Net, "[NetClient] Network thread exiting");
		if (ssl != nullptr) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
		CloseSocket(socketHandle);
		WSACleanup();
	}
}
