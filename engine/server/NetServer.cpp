#if defined(__linux__)

#include "engine/server/NetServer.h"

#include "engine/network/ErrorPacket.h"
#include "engine/network/NetErrorCode.h"
#include "engine/network/PacketView.h"
#include "engine/network/ProtocolV1Constants.h"
#include "engine/network/NetworkBufferPool.h"
#include "engine/server/ServerProtocol.h"
#include "engine/server/ConnectionDDoSProtector.h"

#include "engine/core/Log.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace engine::server
{
	namespace
	{
		constexpr size_t kRxBufferCapacity = 2 * 16384u;
		constexpr int kEpollMaxEvents = 128;
		constexpr int kListenBacklog = 128;
		// Simple v1: Snapshot messages are considered "state" (lower priority).
		inline bool IsStateMessageOpcode(uint16_t opcode)
		{
			return opcode == static_cast<uint16_t>(MessageKind::Snapshot);
		}
		/// Max ERROR packets sent per connection before disconnecting (anti-spam).
		constexpr uint32_t kMaxErrorPacketsPerConnection = 10u;

		/// Set fd to non-blocking. Returns false on error.
		bool SetNonBlocking(int fd)
		{
			int flags = fcntl(fd, F_GETFL, 0);
			if (flags == -1)
				return false;
			if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
				return false;
			return true;
		}

		static std::string FormatIpv4Port(uint32_t ipHostOrder, uint16_t portHost)
		{
			char buf[48];
			const unsigned a = (ipHostOrder >> 24) & 0xFFu;
			const unsigned b = (ipHostOrder >> 16) & 0xFFu;
			const unsigned c = (ipHostOrder >> 8) & 0xFFu;
			const unsigned d = ipHostOrder & 0xFFu;
			std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u", a, b, c, d, static_cast<unsigned>(portHost));
			return std::string(buf);
		}
	}

	/// TLS handshake state per connection (when TLS enabled).
	enum class TlsHandshakeState
	{
		InProgress,
		Ready,
		Failed
	};

	struct NetServer::Impl
	{
		NetServerConfig config;
		uint16_t listenPort = 0;
		int listenFd = -1;
		int epollFd = -1;
		std::atomic<bool> running{ false };
		std::thread ioThread;
		// M25.4: per-IP / accept-throttle + temporary deny helper (used from IO thread only).
		ConnectionDDoSProtector ddosProtector;
		std::chrono::steady_clock::time_point ipPurgeLast = std::chrono::steady_clock::now();
		std::chrono::steady_clock::time_point acceptThrottleLastLog = std::chrono::steady_clock::now();
		uint64_t acceptThrottleRejects = 0;

		bool tlsEnabled = false;
		SSL_CTX* sslCtx = nullptr;

		std::atomic<uint32_t> connectionCount{ 0 };
		std::atomic<uint32_t> nextConnId{ 1 };
		std::mutex connMutex;
		struct Conn
		{
			int fd = -1;
			uint32_t connId = 0;
			// Remote IPv4 address in host byte order (for DDoS tracking).
			uint32_t ipHostOrder = 0u;
			uint16_t remotePortHost = 0u;
			std::chrono::steady_clock::time_point connectedAt{};
			size_t rxBytesAccum = 0;
			SSL* ssl = nullptr;
			TlsHandshakeState handshakeState = TlsHandshakeState::Ready;
			std::vector<uint8_t> rxBuffer;
			size_t rxConsumed = 0;
			// Simple v1 priority: control queue first, state queue second.
			struct QueuedTxPacket
			{
				engine::network::NetworkBufferPool::PooledBuffer packet;
				size_t offset = 0u; // bytes already written
			};
			std::deque<QueuedTxPacket> txQueueControl;
			std::deque<QueuedTxPacket> txQueueState;
			size_t txQueuedBytes = 0;
			bool wantEpollOut = false;
			uint32_t errorPacketsSent = 0;
			// Token bucket (bytes/sec) for TX throttling.
			double txTokenBucketTokensBytes = 0.0;
			std::chrono::steady_clock::time_point txTokenBucketLastRefill{};
			// Token bucket (packet rate limit per connection).
			double tokenBucketTokens = 0.0;
			std::chrono::steady_clock::time_point tokenBucketLastRefill{ std::chrono::steady_clock::now() };
			// Decode failure count; disconnect when >= threshold.
			uint32_t decodeFailureCount = 0;
			// TLS handshake deadline (only meaningful when handshakeState == InProgress).
			std::chrono::steady_clock::time_point handshakeDeadline{};
		};
		std::unordered_map<int, Conn> connections;
		std::unordered_map<uint32_t, int> connIdToFd;

		static constexpr size_t kDisconnectReasonCount = static_cast<size_t>(DisconnectReason::Count);
		std::atomic<uint64_t> disconnectCounts[kDisconnectReasonCount]{};

		/// M19.11 network counters (updated from IO thread; read via GetNetworkStats).
		std::atomic<uint64_t> connectionsTotal{ 0 };
		std::atomic<uint64_t> handshakeSuccess{ 0 };
		std::atomic<uint64_t> handshakeFail{ 0 };
		std::atomic<uint64_t> bytesIn{ 0 };
		std::atomic<uint64_t> bytesOut{ 0 };
		std::atomic<uint64_t> packetsIn{ 0 };
		std::atomic<uint64_t> packetsOut{ 0 };
		std::atomic<uint64_t> packetsDropped{ 0 };

		std::mutex handlerMutex;
		NetServerPacketHandler packetHandler;

		std::vector<std::thread> workers;
		std::atomic<bool> workersQuit{ false };
		std::mutex jobMutex;
		std::condition_variable jobCv;
		struct PacketJob
		{
			uint32_t connId = 0;
			uint16_t opcode = 0;
			uint32_t requestId = 0;
			uint64_t sessionId = 0;
			engine::network::NetworkBufferPool::PooledBuffer payload;
		};
		std::queue<PacketJob> jobQueue;

		void IoThreadRun();
		void WorkerThreadRun();
		void CloseConnection(int fd, DisconnectReason reason);
		void MaybeModifyEpollOut(int fd, bool wantOut);
		/// Set epoll events for client fd (EPOLLIN/EPOLLOUT). Call with connMutex held.
		void SetEpollEvents(int fd, uint32_t events);
		/// Log OpenSSL error queue for the current thread (handshake/read/write errors).
		void LogSslErrors(const char* context);
		/// Returns standardised string for logging. Call with connMutex not held.
		static const char* DisconnectReasonString(DisconnectReason r);
	};

	inline const char* NetServer::Impl::DisconnectReasonString(DisconnectReason r)
	{
		switch (r)
		{
		case DisconnectReason::PeerClosed: return "peer_closed";
		case DisconnectReason::EpollErr: return "EPOLLERR";
		case DisconnectReason::EpollHup: return "EPOLLHUP";
		case DisconnectReason::InvalidPacket: return "invalid_packet";
		case DisconnectReason::DecodeFailures: return "decode_failures";
		case DisconnectReason::RateLimit: return "rate_limit";
		case DisconnectReason::HandshakeTimeout: return "handshake_timeout";
		case DisconnectReason::TlsHandshakeFailed: return "TLS_handshake_failed";
		case DisconnectReason::SslReadError: return "SSL_read_error";
		case DisconnectReason::SslWriteError: return "SSL_write_error";
		case DisconnectReason::RecvError: return "recv_error";
		case DisconnectReason::SendError: return "send_error";
		case DisconnectReason::TxQueueCap: return "tx_queue_cap";
		case DisconnectReason::HeartbeatTimeout: return "heartbeat_timeout";
		default: return "unknown";
	}
}

	/// Call without holding connMutex (locks internally). Frees SSL if present (SSL_shutdown + SSL_free), then closes fd.
	void NetServer::Impl::CloseConnection(int fd, DisconnectReason reason)
	{
		uint32_t idx = static_cast<uint32_t>(reason);
		if (idx < kDisconnectReasonCount)
			disconnectCounts[idx].fetch_add(1, std::memory_order_relaxed);
		SSL* sslToFree = nullptr;
		uint32_t ipHostOrder = 0u;
		uint16_t remotePortHost = 0u;
		uint32_t connId = 0u;
		size_t rxBytesAccum = 0u;
		double aliveSec = 0.0;
		bool hadConn = false;
		{
			std::lock_guard lock(connMutex);
			auto it = connections.find(fd);
			if (it != connections.end())
			{
				hadConn = true;
				connId = it->second.connId;
				ipHostOrder = it->second.ipHostOrder;
				remotePortHost = it->second.remotePortHost;
				rxBytesAccum = it->second.rxBytesAccum;
				const auto now = std::chrono::steady_clock::now();
				aliveSec = std::chrono::duration<double>(now - it->second.connectedAt).count();
				LOG_DEBUG(Server, "[NETSRV] CloseConnection connId={} reason={}", connId, DisconnectReasonString(reason));
				connIdToFd.erase(it->second.connId);
				sslToFree = it->second.ssl;
				connections.erase(it);
				connectionCount.store(static_cast<uint32_t>(connections.size()));
			}
		}

		// M25.4: update DDoS tracking regardless of other close causes.
		if (ipHostOrder != 0u)
			ddosProtector.OnConnectionClosed(ipHostOrder, reason, std::chrono::steady_clock::now());

		if (sslToFree != nullptr)
		{
			SSL_shutdown(sslToFree);
			SSL_free(sslToFree);
		}
		epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
		close(fd);
		if (hadConn)
		{
			const std::string peer = FormatIpv4Port(ipHostOrder, remotePortHost);
			const char* zeroHint = (reason == DisconnectReason::PeerClosed && rxBytesAccum == 0u)
				? " | note: 0 byte received then peer closed — often TCP healthcheck/scanner, or HTTP client on game port"
				: "";
			LOG_INFO(Net,
				"[NetServer] Connection closed (connId={}, fd={}, peer={}, reason={}, alive_s={:.3f}, rx_bytes={}){}",
				connId,
				fd,
				peer,
				DisconnectReasonString(reason),
				aliveSec,
				rxBytesAccum,
				zeroHint);
		}
		else
		{
			LOG_INFO(Net, "[NetServer] Connection closed (fd={}, reason={}, peer=unknown — no connection state)", fd, DisconnectReasonString(reason));
		}
	}

	/// Call with connMutex held (same thread).
	void NetServer::Impl::MaybeModifyEpollOut(int fd, bool wantOut)
	{
		auto it = connections.find(fd);
		if (it == connections.end())
			return;
		Conn& c = it->second;
		if (c.wantEpollOut == wantOut)
			return;
		c.wantEpollOut = wantOut;
		uint32_t ev = EPOLLIN | (wantOut ? EPOLLOUT : 0);
		SetEpollEvents(fd, ev);
	}

	void NetServer::Impl::SetEpollEvents(int fd, uint32_t events)
	{
		epoll_event ev{};
		ev.events = events;
		ev.data.fd = fd;
		if (epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &ev) != 0)
			LOG_WARN(Net, "[NetServer] epoll_ctl MOD failed (fd={})", fd);
	}

	void NetServer::Impl::LogSslErrors(const char* context)
	{
		unsigned long err;
		while ((err = ERR_get_error()) != 0)
		{
			char buf[256];
			ERR_error_string_n(err, buf, sizeof(buf));
			LOG_ERROR(Net, "[NetServer] {}: {}", context, buf);
		}
	}

	void NetServer::Impl::IoThreadRun()
	{
		LOG_INFO(Server, "[NETSRV] IoThread started");
		epoll_event events[kEpollMaxEvents];

		while (running.load())
		{
			int n = epoll_wait(epollFd, events, kEpollMaxEvents, 100);
			if (n == -1)
			{
				if (errno == EINTR)
					continue;
				LOG_ERROR(Net, "[NetServer] epoll_wait failed: {}", strerror(errno));
				break;
			}

			// M25.4: periodic purge of IP tracking structures.
			{
				const auto now = std::chrono::steady_clock::now();
				constexpr auto kIpPurgePeriod = std::chrono::seconds(10);
				if (now - ipPurgeLast >= kIpPurgePeriod)
				{
					ddosProtector.PurgeExpired(now);
					ipPurgeLast = now;
				}
			}

			for (int i = 0; i < n && running.load(); ++i)
			{
				int fd = events[i].data.fd;
				uint32_t evFlags = events[i].events;

				if (fd == listenFd)
				{
					// Accept
					for (;;)
					{
						auto now = std::chrono::steady_clock::now();
						if (!ddosProtector.TryConsumeAcceptToken(now))
						{
							// Rate limited log to avoid flooding during accept storms.
							if (now - acceptThrottleLastLog >= std::chrono::seconds(1))
							{
								acceptThrottleLastLog = now;
								LOG_WARN(Net,
									"[DDoS] accept throttle engaged (maxAcceptsPerSec={})",
									config.maxAcceptsPerSec);
							}
							break;
						}

						sockaddr_in clientAddr{};
						socklen_t clientLen = sizeof(clientAddr);
						int clientFd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
						if (clientFd == -1)
						{
							if (errno == EAGAIN || errno == EWOULDBLOCK)
								break;
							LOG_WARN(Net, "[NetServer] accept failed: {}", strerror(errno));
							break;
						}
						const uint32_t ipHostOrder = ntohl(clientAddr.sin_addr.s_addr);

						if (connectionCount.load() >= config.maxConnections)
						{
							LOG_WARN(Net, "[NetServer] Rejecting connection (max_connections={})", config.maxConnections);
							close(clientFd);
							continue;
						}

						// Apply DDoS accept protection before expensive TLS setup.
						if (!ddosProtector.TryAcceptForIp(ipHostOrder, now))
						{
							close(clientFd);
							continue;
						}

						if (!SetNonBlocking(clientFd))
						{
							LOG_ERROR(Net, "[NetServer] SetNonBlocking failed for new fd {}", clientFd);
							ddosProtector.OnConnectionClosed(ipHostOrder, DisconnectReason::PeerClosed, now);
							close(clientFd);
							continue;
						}
						uint32_t connId = nextConnId.fetch_add(1);
						epoll_event ev{};
						ev.events = EPOLLIN;
						ev.data.fd = clientFd;
						if (epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &ev) != 0)
						{
							LOG_ERROR(Net, "[NetServer] epoll_ctl ADD failed (fd={})", clientFd);
							ddosProtector.OnConnectionClosed(ipHostOrder, DisconnectReason::PeerClosed, now);
							close(clientFd);
							continue;
						}
						{
							std::lock_guard lock(connMutex);
							Conn& c = connections[clientFd];
							c.fd = clientFd;
							c.connId = connId;
							c.ipHostOrder = ipHostOrder;
							c.remotePortHost = ntohs(clientAddr.sin_port);
							c.connectedAt = now;
							c.rxBytesAccum = 0;
							connIdToFd[connId] = clientFd;
							c.rxBuffer.reserve(kRxBufferCapacity);
							c.tokenBucketTokens = config.packetBurst;
							c.tokenBucketLastRefill = std::chrono::steady_clock::now();
							c.txTokenBucketTokensBytes = config.maxBandwidthPerPlayerBytesPerSec;
							c.txTokenBucketLastRefill = std::chrono::steady_clock::now();
							if (tlsEnabled && sslCtx != nullptr)
							{
								c.ssl = SSL_new(sslCtx);
								SSL_set_fd(c.ssl, clientFd);
								c.handshakeState = TlsHandshakeState::InProgress;
								c.handshakeDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(config.handshakeTimeoutSec);
							}
							connectionCount.store(static_cast<uint32_t>(connections.size()));
							connectionsTotal.fetch_add(1, std::memory_order_relaxed);
						}
						LOG_INFO(Net,
							"[NetServer] TCP accept (connId={}, fd={}, peer={})",
							connId,
							clientFd,
							FormatIpv4Port(ipHostOrder, ntohs(clientAddr.sin_port)));
						if (tlsEnabled && sslCtx != nullptr)
						{
							std::unique_lock lock(connMutex);
							auto it = connections.find(clientFd);
							if (it != connections.end() && it->second.ssl != nullptr)
							{
								Conn& c = it->second;
								int ret = SSL_accept(c.ssl);
								if (ret == 1)
								{
									c.handshakeState = TlsHandshakeState::Ready;
									handshakeSuccess.fetch_add(1, std::memory_order_relaxed);
									SetEpollEvents(clientFd, EPOLLIN);
								}
								else
								{
									int err = SSL_get_error(c.ssl, ret);
									if (err == SSL_ERROR_WANT_READ)
										SetEpollEvents(clientFd, EPOLLIN);
									else if (err == SSL_ERROR_WANT_WRITE)
										SetEpollEvents(clientFd, EPOLLOUT);
									else
									{
										LogSslErrors("TLS handshake");
										handshakeFail.fetch_add(1, std::memory_order_relaxed);
										lock.unlock();
										CloseConnection(clientFd, DisconnectReason::TlsHandshakeFailed);
									}
								}
							}
						}
					}
					continue;
				}

				// Client fd
				if (evFlags & (EPOLLERR | EPOLLHUP))
				{
					CloseConnection(fd, (evFlags & EPOLLERR) ? DisconnectReason::EpollErr : DisconnectReason::EpollHup);
					continue;
				}

				std::unique_lock lock(connMutex);
				auto it = connections.find(fd);
				if (it == connections.end())
					continue;
				Conn& c = it->second;

				// TLS handshake in progress: advance and set epoll interests from WANT_READ/WANT_WRITE
				if (c.handshakeState == TlsHandshakeState::InProgress && c.ssl != nullptr)
				{
					int ret = SSL_accept(c.ssl);
					if (ret == 1)
					{
						c.handshakeState = TlsHandshakeState::Ready;
						handshakeSuccess.fetch_add(1, std::memory_order_relaxed);
						SetEpollEvents(fd, EPOLLIN | (c.wantEpollOut ? EPOLLOUT : 0));
					}
					else
					{
						int err = SSL_get_error(c.ssl, ret);
						if (err == SSL_ERROR_WANT_READ)
							SetEpollEvents(fd, EPOLLIN);
						else if (err == SSL_ERROR_WANT_WRITE)
							SetEpollEvents(fd, EPOLLOUT);
						else
						{
							LogSslErrors("TLS handshake");
							handshakeFail.fetch_add(1, std::memory_order_relaxed);
							lock.unlock();
							CloseConnection(fd, DisconnectReason::TlsHandshakeFailed);
							goto next_event;
						}
					}
					// Handshake timeout: close if deadline passed
					if (std::chrono::steady_clock::now() > c.handshakeDeadline)
					{
						LOG_WARN(Net, "[NetServer] TLS handshake timeout (fd={}, connId={})", fd, c.connId);
						handshakeFail.fetch_add(1, std::memory_order_relaxed);
						lock.unlock();
						CloseConnection(fd, DisconnectReason::HandshakeTimeout);
						goto next_event;
					}
					goto next_event;
				}

				// RX
				if (evFlags & EPOLLIN)
				{
					// Compact consumed
					if (c.rxConsumed > 0)
					{
						if (c.rxConsumed >= c.rxBuffer.size())
							c.rxBuffer.clear();
						else
						{
							std::memmove(c.rxBuffer.data(), c.rxBuffer.data() + c.rxConsumed, c.rxBuffer.size() - c.rxConsumed);
							c.rxBuffer.resize(c.rxBuffer.size() - c.rxConsumed);
						}
						c.rxConsumed = 0;
					}

					for (;;)
					{
						char tmp[4096];
						ssize_t received;
						if (c.ssl != nullptr)
							received = static_cast<ssize_t>(SSL_read(c.ssl, tmp, sizeof(tmp)));
						else
							received = recv(fd, tmp, sizeof(tmp), 0);
						if (received > 0)
						{
							c.rxBytesAccum += static_cast<size_t>(received);
							bytesIn.fetch_add(static_cast<size_t>(received), std::memory_order_relaxed);
							size_t prev = c.rxBuffer.size();
							c.rxBuffer.resize(prev + static_cast<size_t>(received));
							std::memcpy(c.rxBuffer.data() + prev, tmp, static_cast<size_t>(received));
						}
						else if (received == 0)
						{
							lock.unlock();
							CloseConnection(fd, DisconnectReason::PeerClosed);
							goto next_event;
						}
						else
						{
							if (c.ssl != nullptr)
							{
								int err = SSL_get_error(c.ssl, static_cast<int>(received));
								if (err == SSL_ERROR_WANT_READ)
								{
									SetEpollEvents(fd, EPOLLIN | (c.wantEpollOut ? EPOLLOUT : 0));
									break;
								}
								if (err == SSL_ERROR_WANT_WRITE)
								{
									SetEpollEvents(fd, EPOLLOUT | EPOLLIN);
									break;
								}
								LogSslErrors("SSL_read");
								lock.unlock();
								CloseConnection(fd, DisconnectReason::SslReadError);
								goto next_event;
							}
							if (errno == EAGAIN || errno == EWOULDBLOCK)
								break;
							lock.unlock();
							CloseConnection(fd, DisconnectReason::RecvError);
							goto next_event;
						}
					}

					// Parse frames
					while (c.rxBuffer.size() >= static_cast<size_t>(engine::network::kProtocolV1HeaderSize))
					{
						engine::network::PacketView view;
						engine::network::PacketParseResult res = engine::network::PacketView::Parse(
							c.rxBuffer.data(), c.rxBuffer.size(), view);
						if (res == engine::network::PacketParseResult::Incomplete)
							break;
						if (res == engine::network::PacketParseResult::Invalid)
						{
							c.decodeFailureCount++;
							packetsDropped.fetch_add(1, std::memory_order_relaxed);
							if (c.decodeFailureCount >= config.decodeFailureThreshold)
							{
								LOG_WARN(Net, "[NetServer] Decode failures threshold (connId={}, count={}), closing", c.connId, c.decodeFailureCount);
								if (lock.owns_lock())
									lock.unlock();
								CloseConnection(fd, DisconnectReason::DecodeFailures);
								goto next_event;
							}
							// Skip 1 byte to make progress; compact buffer so next iteration advances.
							if (c.rxConsumed + 1 >= c.rxBuffer.size())
								c.rxBuffer.clear();
							else
							{
								std::memmove(c.rxBuffer.data(), c.rxBuffer.data() + c.rxConsumed + 1, c.rxBuffer.size() - c.rxConsumed - 1);
								c.rxBuffer.resize(c.rxBuffer.size() - c.rxConsumed - 1);
							}
							c.rxConsumed = 0;
							continue;
						}
						// Ok: token bucket then push job to workers
						{
							auto now = std::chrono::steady_clock::now();
							double elapsed = std::chrono::duration<double>(now - c.tokenBucketLastRefill).count();
							c.tokenBucketTokens = (std::min)(config.packetBurst, c.tokenBucketTokens + elapsed * config.packetRatePerSec);
							c.tokenBucketLastRefill = now;
							if (c.tokenBucketTokens < 1.0)
							{
								packetsDropped.fetch_add(1, std::memory_order_relaxed);
								LOG_WARN(Net, "[NetServer] Rate limit exceeded (connId={}), closing", c.connId);
								if (lock.owns_lock())
									lock.unlock();
								CloseConnection(fd, DisconnectReason::RateLimit);
								goto next_event;
							}
							c.tokenBucketTokens -= 1.0;
						}
						size_t payloadSize = view.PayloadSize();
						engine::network::NetworkBufferPool::PooledBuffer payload;
						if (payloadSize > 0)
						{
							payload = engine::network::NetworkBufferPool::AcquireBuffer(payloadSize);
							if (!payload)
							{
								LOG_ERROR(Net, "[NetServer] RX payload buffer alloc FAILED (payloadSize={}) connId={}",
									payloadSize, c.connId);
								lock.unlock();
								CloseConnection(fd, DisconnectReason::DecodeFailures);
								goto next_event;
							}
							std::memcpy(payload.data(), view.Payload(), payloadSize);
						}
						{
							std::lock_guard jobLock(jobMutex);
							jobQueue.push({ c.connId, view.Opcode(), view.RequestId(), view.SessionId(), std::move(payload) });
						}
						packetsIn.fetch_add(1, std::memory_order_relaxed);
						jobCv.notify_one();
						c.rxConsumed += view.Size();
					}
				}

				// TX
				if (evFlags & EPOLLOUT && (!c.txQueueControl.empty() || !c.txQueueState.empty()))
				{
					while (!c.txQueueControl.empty() || !c.txQueueState.empty())
					{
						// Refill TX token bucket (bytes/sec). Capacity is ~1s burst.
						{
							const auto now = std::chrono::steady_clock::now();
							const double elapsed =
								std::chrono::duration<double>(now - c.txTokenBucketLastRefill).count();
							const double capBytes = config.maxBandwidthPerPlayerBytesPerSec;
							if (capBytes > 0.0)
							{
								c.txTokenBucketTokensBytes =
									(std::min)(capBytes, c.txTokenBucketTokensBytes + elapsed * capBytes);
								c.txTokenBucketLastRefill = now;
							}
						}

						if (c.txTokenBucketTokensBytes < 1.0)
							break;

						auto& activeQueue =
							!c.txQueueControl.empty() ? c.txQueueControl : c.txQueueState;
						auto& front = activeQueue.front();

						const size_t remaining = front.packet.size() - front.offset;
						const size_t allowed =
							(std::min)(remaining, static_cast<size_t>(c.txTokenBucketTokensBytes));
						if (allowed == 0)
							break;
						int sent;
						if (c.ssl != nullptr)
							sent = SSL_write(c.ssl, front.packet.data() + front.offset, static_cast<int>(allowed));
						else
							sent = static_cast<int>(send(fd,
								front.packet.data() + front.offset,
								allowed, MSG_NOSIGNAL));
						if (sent > 0)
						{
							bytesOut.fetch_add(static_cast<size_t>(sent), std::memory_order_relaxed);
							c.txQueuedBytes -= static_cast<size_t>(sent);
							c.txTokenBucketTokensBytes -= static_cast<double>(sent);
							front.offset += static_cast<size_t>(sent);
							if (front.offset >= front.packet.size())
							{
								packetsOut.fetch_add(1, std::memory_order_relaxed);
								activeQueue.pop_front();
							}
						}
						else
						{
							if (c.ssl != nullptr)
							{
								int err = SSL_get_error(c.ssl, sent);
								if (err == SSL_ERROR_WANT_WRITE)
								{
									SetEpollEvents(fd, EPOLLOUT | EPOLLIN);
									break;
								}
								if (err == SSL_ERROR_WANT_READ)
								{
									SetEpollEvents(fd, EPOLLIN);
									break;
								}
								LogSslErrors("SSL_write");
								lock.unlock();
								CloseConnection(fd, DisconnectReason::SslWriteError);
								goto next_event;
							}
							if (errno == EAGAIN || errno == EWOULDBLOCK)
								break;
							LOG_WARN(Net, "[NetServer] send failed (fd={}): {}", fd, strerror(errno));
							lock.unlock();
							CloseConnection(fd, DisconnectReason::SendError);
							goto next_event;
						}
					}
					if (c.txQueueControl.empty() && c.txQueueState.empty())
						MaybeModifyEpollOut(fd, false);
				}
			next_event:;
			}
		}

		LOG_DEBUG(Server, "[NETSRV] IoThread exiting");
		// Cleanup all connections on exit (SSL_shutdown + SSL_free + close)
		{
			std::lock_guard lock(connMutex);
			for (auto& [f, conn] : connections)
			{
				if (conn.ssl != nullptr)
				{
					SSL_shutdown(conn.ssl);
					SSL_free(conn.ssl);
					conn.ssl = nullptr;
				}
				epoll_ctl(epollFd, EPOLL_CTL_DEL, f, nullptr);
				close(f);
			}
			connections.clear();
			connectionCount.store(0);
		}
	}

	void NetServer::Impl::WorkerThreadRun()
	{
		LOG_INFO(Server, "[NETSRV] WorkerThread started");
		while (!workersQuit.load())
		{
			PacketJob job;
			{
				std::unique_lock lock(jobMutex);
				jobCv.wait_for(lock, std::chrono::milliseconds(10), [this] { return workersQuit.load() || !jobQueue.empty(); });
				if (workersQuit.load())
					break;
				if (jobQueue.empty())
					continue;
				job = std::move(jobQueue.front());
				jobQueue.pop();
			}
			NetServerPacketHandler h;
			{
				std::lock_guard lock(handlerMutex);
				h = packetHandler;
			}
			if (h)
				h(job.connId, job.opcode, job.requestId, job.sessionId, job.payload.data(), job.payload.size());
		}
		LOG_DEBUG(Server, "[NETSRV] WorkerThread exiting");
	}

	NetServer::~NetServer()
	{
		Shutdown();
	}

	bool NetServer::Init(uint16_t listenPort, const NetServerConfig& config)
	{
		LOG_INFO(Server, "[NETSRV] Init enter port={}", listenPort);
		if (m_impl != nullptr)
		{
			LOG_WARN(Net, "[NetServer] Init ignored: already initialized");
			return true;
		}

		m_impl = new Impl();
		m_impl->config = config;
		if (m_impl->config.maxBandwidthPerPlayerBytesPerSec <= 0.0)
		{
			const double derivedBytesPerSec =
				m_impl->config.packetRatePerSec * static_cast<double>(engine::network::kProtocolV1MaxPacketSize);
			m_impl->config.maxBandwidthPerPlayerBytesPerSec = derivedBytesPerSec < 1.0 ? 1.0 : derivedBytesPerSec;
			LOG_WARN(Net,
				"[NetServer] max_bandwidth_per_player disabled/invalid -> using derived TX cap (bytes/sec={})",
				m_impl->config.maxBandwidthPerPlayerBytesPerSec);
		}

		// M25.3: Init the shared RX/TX buffer pool (4KB/16KB).
		if (!engine::network::NetworkBufferPool::Init())
		{
			LOG_ERROR(Net, "[NetServer] BufferPool Init FAILED");
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		// M25.4: Init per-IP connection throttle + handshake deny helper.
		{
			ConnectionDDoSProtector::Config ddosCfg;
			ddosCfg.maxConnectionsPerIp = m_impl->config.maxConnectionsPerIp;
			ddosCfg.maxAcceptsPerSec = m_impl->config.maxAcceptsPerSec;
			ddosCfg.handshakeFailuresBeforeDeny = m_impl->config.handshakeFailuresBeforeDeny;
			ddosCfg.handshakeDenyDurationSec = m_impl->config.handshakeDenyDurationSec;
			if (!m_impl->ddosProtector.Init(ddosCfg))
			{
				LOG_ERROR(Net, "[NetServer] DDoS Protector init FAILED");
				delete m_impl;
				m_impl = nullptr;
				return false;
			}
		}
		m_impl->listenPort = listenPort;

		m_impl->listenFd = socket(AF_INET, SOCK_STREAM, 0);
		LOG_DEBUG(Server, "[NETSRV] socket() fd={}", m_impl->listenFd);
		if (m_impl->listenFd == -1)
		{
			LOG_ERROR(Net, "[NetServer] Init FAILED: socket() failed: {}", strerror(errno));
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		int opt = 1;
		if (setsockopt(m_impl->listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0)
		{
			LOG_WARN(Net, "[NetServer] setsockopt(SO_REUSEADDR) failed: {}", strerror(errno));
		}

		if (!SetNonBlocking(m_impl->listenFd))
		{
			LOG_ERROR(Net, "[NetServer] Init FAILED: SetNonBlocking(listen) failed");
			close(m_impl->listenFd);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(listenPort);
		addr.sin_addr.s_addr = INADDR_ANY;
		if (bind(m_impl->listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
		{
			LOG_ERROR(Net, "[NetServer] Init FAILED: bind(port={}) failed: {}", listenPort, strerror(errno));
			close(m_impl->listenFd);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}
		LOG_INFO(Server, "[NETSRV] bind() OK");

		if (listen(m_impl->listenFd, kListenBacklog) != 0)
		{
			LOG_ERROR(Net, "[NetServer] Init FAILED: listen() failed: {}", strerror(errno));
			close(m_impl->listenFd);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}
		LOG_INFO(Server, "[NETSRV] listen() OK");

		m_impl->epollFd = epoll_create1(EPOLL_CLOEXEC);
		LOG_DEBUG(Server, "[NETSRV] epoll_create1 epollFd={}", m_impl->epollFd);
		if (m_impl->epollFd == -1)
		{
			LOG_ERROR(Net, "[NetServer] Init FAILED: epoll_create1 failed: {}", strerror(errno));
			close(m_impl->listenFd);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		epoll_event ev{};
		ev.events = EPOLLIN;
		ev.data.fd = m_impl->listenFd;
		if (epoll_ctl(m_impl->epollFd, EPOLL_CTL_ADD, m_impl->listenFd, &ev) != 0)
		{
			LOG_ERROR(Net, "[NetServer] Init FAILED: epoll_ctl ADD listen failed: {}", strerror(errno));
			close(m_impl->epollFd);
			close(m_impl->listenFd);
			delete m_impl;
			m_impl = nullptr;
			return false;
		}

		if (!config.tlsCertPath.empty() && !config.tlsKeyPath.empty())
		{
			SSL_library_init();
			SSL_load_error_strings();
			OpenSSL_add_all_algorithms();
			const SSL_METHOD* method = TLS_server_method();
			m_impl->sslCtx = SSL_CTX_new(method);
			if (m_impl->sslCtx == nullptr)
			{
				m_impl->LogSslErrors("SSL_CTX_new");
				LOG_ERROR(Net, "[NetServer] Init FAILED: SSL_CTX_new failed");
				close(m_impl->epollFd);
				close(m_impl->listenFd);
				delete m_impl;
				m_impl = nullptr;
				return false;
			}
			SSL_CTX_set_min_proto_version(m_impl->sslCtx, TLS1_2_VERSION);
			if (SSL_CTX_use_certificate_file(m_impl->sslCtx, config.tlsCertPath.c_str(), SSL_FILETYPE_PEM) <= 0)
			{
				m_impl->LogSslErrors("SSL_CTX_use_certificate_file");
				LOG_ERROR(Net, "[NetServer] Init FAILED: certificate file ({})", config.tlsCertPath);
				SSL_CTX_free(m_impl->sslCtx);
				m_impl->sslCtx = nullptr;
				close(m_impl->epollFd);
				close(m_impl->listenFd);
				delete m_impl;
				m_impl = nullptr;
				return false;
			}
			if (SSL_CTX_use_PrivateKey_file(m_impl->sslCtx, config.tlsKeyPath.c_str(), SSL_FILETYPE_PEM) <= 0)
			{
				m_impl->LogSslErrors("SSL_CTX_use_PrivateKey_file");
				LOG_ERROR(Net, "[NetServer] Init FAILED: private key file ({})", config.tlsKeyPath);
				SSL_CTX_free(m_impl->sslCtx);
				m_impl->sslCtx = nullptr;
				close(m_impl->epollFd);
				close(m_impl->listenFd);
				delete m_impl;
				m_impl = nullptr;
				return false;
			}
			m_impl->tlsEnabled = true;
			LOG_INFO(Net, "[NetServer] TLS enabled (cert={}, key={}, min=TLS1.2)", config.tlsCertPath, config.tlsKeyPath);
		}

		m_impl->running.store(true);
		for (uint32_t i = 0; i < config.workerThreadCount; ++i)
			m_impl->workers.emplace_back(&Impl::WorkerThreadRun, m_impl);
		m_impl->ioThread = std::thread(&Impl::IoThreadRun, m_impl);

		LOG_INFO(Net, "[NetServer] Init OK (port={}, max_connections={}, workers={}, tls={}, rate_limit={}/s burst={}, decode_threshold={}, handshake_timeout_s={}, tx_cap={})",
			listenPort, config.maxConnections, config.workerThreadCount, m_impl->tlsEnabled ? "on" : "off",
			config.packetRatePerSec, config.packetBurst, config.decodeFailureThreshold, config.handshakeTimeoutSec, config.maxQueuedTxBytesPerConnection);

		LOG_INFO(Net, "[NetServer] TX bandwidth cap enabled (bytes/sec={})",
			config.maxBandwidthPerPlayerBytesPerSec);
		return true;
	}

	void NetServer::Shutdown()
	{
		LOG_DEBUG(Server, "[NETSRV] Shutdown enter");
		if (m_impl == nullptr)
			return;

		m_impl->running.store(false);
		LOG_DEBUG(Server, "[NETSRV] running=false, avant ioThread.joi");
		if (m_impl->ioThread.joinable())
			m_impl->ioThread.join();
		LOG_DEBUG(Server, "[NETSRV] ioThread joined");

		m_impl->workersQuit.store(true);
		m_impl->jobCv.notify_all();
		for (auto& w : m_impl->workers)
			if (w.joinable())
				w.join();
		LOG_DEBUG(Server, "[NETSRV] workers joined count={}", m_impl->workers.size());
		m_impl->workers.clear();

		if (m_impl->sslCtx != nullptr)
		{
			SSL_CTX_free(m_impl->sslCtx);
			m_impl->sslCtx = nullptr;
			LOG_INFO(Server, "[NETSRV] SSL_CTX_free OK");
		}
		if (m_impl->epollFd != -1)
		{
			close(m_impl->epollFd);
			m_impl->epollFd = -1;
		}
		if (m_impl->listenFd != -1)
		{
			close(m_impl->listenFd);
			m_impl->listenFd = -1;
		}

		LOG_INFO(Net, "[NetServer] Destroyed (port={})", m_impl->listenPort);
		delete m_impl;
		m_impl = nullptr;

		// M25.3 metrics: buffer-pool allocations saved (estimate).
		{
			engine::network::NetworkBufferPool::Metrics m =
				engine::network::NetworkBufferPool::ConsumeMetrics();
			const uint64_t allocSavedEst = m.reuseHits; // reused buffers avoided "new allocation" events.
			LOG_INFO(Net,
				"[NetServer][BufferPool] metrics acquires={} reuseHits={} newAllocs={} releases={} allocSavedEst={}",
				m.acquires, m.reuseHits, m.newAllocs, m.releases, allocSavedEst);
		}
	}

	bool NetServer::Send(uint32_t connId, std::span<const uint8_t> packet)
	{
		if (m_impl == nullptr)
			return false;
		if (packet.size() < engine::network::kProtocolV1HeaderSize || packet.size() > engine::network::kProtocolV1MaxPacketSize)
			return false;

		// TX priority classification based on protocol v1 opcode (MessageKind in ServerProtocol).
		// Packet header layout: [u16 size][u16 opcode][u16 flags][u32 requestId][u64 sessionId]
		const uint16_t opcode =
			static_cast<uint16_t>(packet[2] | (static_cast<uint16_t>(packet[3]) << 8));
		const bool isStateMessage = IsStateMessageOpcode(opcode);
		std::lock_guard lock(m_impl->connMutex);
		for (auto& [fd, c] : m_impl->connections)
		{
			if (c.connId != connId)
				continue;
			if (c.txQueuedBytes + packet.size() > m_impl->config.maxQueuedTxBytesPerConnection)
			{
				m_impl->disconnectCounts[static_cast<size_t>(DisconnectReason::TxQueueCap)].fetch_add(1, std::memory_order_relaxed);
				LOG_WARN(Net, "[NetServer] Backpressure: connId={} TX queue cap exceeded, closing (reason={})", connId, Impl::DisconnectReasonString(DisconnectReason::TxQueueCap));
				m_impl->connIdToFd.erase(connId);
				SSL* sslToFree = c.ssl;
				c.ssl = nullptr;
				m_impl->connections.erase(fd);
				m_impl->connectionCount.store(static_cast<uint32_t>(m_impl->connections.size()));
				if (sslToFree != nullptr)
				{
					SSL_shutdown(sslToFree);
					SSL_free(sslToFree);
				}
				epoll_ctl(m_impl->epollFd, EPOLL_CTL_DEL, fd, nullptr);
				close(fd);
				return false;
			}

			// M25.3: Pool RX/TX packet storage to avoid per-send heap allocations.
			engine::network::NetworkBufferPool::PooledBuffer pooledPacket =
				engine::network::NetworkBufferPool::AcquireBuffer(packet.size());
			if (!pooledPacket)
			{
				m_impl->disconnectCounts[static_cast<size_t>(DisconnectReason::SendError)].fetch_add(1, std::memory_order_relaxed);
				LOG_ERROR(Net, "[NetServer] TX pooled buffer alloc FAILED (size={}) connId={}",
					packet.size(), connId);
				m_impl->connIdToFd.erase(connId);
				SSL* sslToFree = c.ssl;
				c.ssl = nullptr;
				m_impl->connections.erase(fd);
				m_impl->connectionCount.store(static_cast<uint32_t>(m_impl->connections.size()));
				if (sslToFree != nullptr)
				{
					SSL_shutdown(sslToFree);
					SSL_free(sslToFree);
				}
				epoll_ctl(m_impl->epollFd, EPOLL_CTL_DEL, fd, nullptr);
				close(fd);
				return false;
			}
			std::memcpy(pooledPacket.data(), packet.data(), packet.size());

			c.txQueuedBytes += packet.size();
			if (isStateMessage)
				c.txQueueState.push_back({ std::move(pooledPacket), 0u });
			else
				c.txQueueControl.push_back({ std::move(pooledPacket), 0u });
			m_impl->MaybeModifyEpollOut(fd, true);
			return true;
		}
		return false;
	}

	void NetServer::CloseConnection(uint32_t connId, DisconnectReason reason)
	{
		if (m_impl == nullptr)
			return;
		int fd = -1;
		{
			std::lock_guard lock(m_impl->connMutex);
			auto it = m_impl->connIdToFd.find(connId);
			if (it == m_impl->connIdToFd.end())
				return;
			fd = it->second;
		}
		m_impl->CloseConnection(fd, reason);
	}

	void NetServer::SetPacketHandler(NetServerPacketHandler handler)
	{
		if (m_impl != nullptr)
		{
			std::lock_guard lock(m_impl->handlerMutex);
			m_impl->packetHandler = std::move(handler);
		}
	}

	uint32_t NetServer::GetConnectionCount() const
	{
		return m_impl != nullptr ? m_impl->connectionCount.load() : 0;
	}

	bool NetServer::IsRunning() const
	{
		return m_impl != nullptr && m_impl->running.load();
	}

	uint64_t NetServer::GetDisconnectCount(DisconnectReason reason) const
	{
		if (m_impl == nullptr)
			return 0;
		uint32_t idx = static_cast<uint32_t>(reason);
		if (idx >= static_cast<uint32_t>(DisconnectReason::Count))
			return 0;
		return m_impl->disconnectCounts[idx].load(std::memory_order_relaxed);
	}

	void NetServer::GetNetworkStats(NetServerStats& out) const
	{
		out = NetServerStats{};
		if (m_impl == nullptr)
			return;
		out.connectionsActive = m_impl->connectionCount.load(std::memory_order_relaxed);
		out.connectionsTotal = m_impl->connectionsTotal.load(std::memory_order_relaxed);
		out.handshakeSuccess = m_impl->handshakeSuccess.load(std::memory_order_relaxed);
		out.handshakeFail = m_impl->handshakeFail.load(std::memory_order_relaxed);
		out.bytesIn = m_impl->bytesIn.load(std::memory_order_relaxed);
		out.bytesOut = m_impl->bytesOut.load(std::memory_order_relaxed);
		out.packetsIn = m_impl->packetsIn.load(std::memory_order_relaxed);
		out.packetsOut = m_impl->packetsOut.load(std::memory_order_relaxed);
		out.packetsDropped = m_impl->packetsDropped.load(std::memory_order_relaxed);
		const size_t n = static_cast<size_t>(DisconnectReason::Count);
		for (size_t i = 0; i < n; ++i)
			out.disconnectByReason[i] = m_impl->disconnectCounts[i].load(std::memory_order_relaxed);
	}
}

#else

#include "engine/server/NetServer.h"

#include "engine/core/Log.h"

namespace engine::server
{
	NetServer::~NetServer() = default;

	bool NetServer::Init(uint16_t /*listenPort*/, const NetServerConfig& /*config*/)
	{
		LOG_ERROR(Net, "[NetServer] Init FAILED: NetServer is Linux-only (epoll)");
		return false;
	}

	void NetServer::Shutdown() {}

	bool NetServer::Send(uint32_t /*connId*/, std::span<const uint8_t> /*packet*/) { return false; }

	void NetServer::CloseConnection(uint32_t /*connId*/, DisconnectReason /*reason*/) {}

	void NetServer::SetPacketHandler(NetServerPacketHandler /*handler*/) {}

	uint32_t NetServer::GetConnectionCount() const { return 0; }

	bool NetServer::IsRunning() const { return false; }

	uint64_t NetServer::GetDisconnectCount(DisconnectReason /*reason*/) const { return 0; }

	void NetServer::GetNetworkStats(NetServerStats& out) const { out = NetServerStats{}; }
}

#endif
