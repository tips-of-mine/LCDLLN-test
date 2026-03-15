#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace engine::server
{
	/// Standardised disconnect reason for transport hardening stats. Stable for logging/observability.
	enum class DisconnectReason : uint32_t
	{
		PeerClosed,
		EpollErr,
		EpollHup,
		InvalidPacket,
		DecodeFailures,
		RateLimit,
		HandshakeTimeout,
		TlsHandshakeFailed,
		SslReadError,
		SslWriteError,
		RecvError,
		SendError,
		TxQueueCap,
		Count
	};

	/// Configuration for the TCP NetServer (Linux epoll). Limits and worker pool size.
	/// When tlsCertPath and tlsKeyPath are both non-empty, TLS is enabled (TLS 1.2+ only); no plain mode on that port.
	struct NetServerConfig
	{
		/// Maximum number of concurrent connections. New connections beyond this are closed.
		uint32_t maxConnections = 1000u;
		/// Maximum queued TX bytes per connection. Connection is closed if exceeded (backpressure).
		size_t maxQueuedTxBytesPerConnection = 256 * 1024u;
		/// Number of worker threads for packet processing (must not block IO).
		uint32_t workerThreadCount = 4u;
		/// Path to TLS server certificate file (PEM). If non-empty with tlsKeyPath, TLS is enabled.
		std::string tlsCertPath;
		/// Path to TLS server private key file (PEM). If non-empty with tlsCertPath, TLS is enabled.
		std::string tlsKeyPath;
		/// Token bucket: sustained packet rate (packets per second) per connection. Ex: 200.
		double packetRatePerSec = 200.0;
		/// Token bucket: max burst (tokens). Ex: 400.
		double packetBurst = 400.0;
		/// Decode (invalid packet) failure threshold; after this many, connection is closed.
		uint32_t decodeFailureThreshold = 5u;
		/// TLS handshake timeout in seconds. Connection closed if handshake not completed in time.
		uint32_t handshakeTimeoutSec = 10u;
	};

	/// Callback invoked from worker threads when a full packet is received. Do not run DB/game logic that blocks.
	using NetServerPacketHandler = std::function<void(uint32_t connId, uint16_t opcode, const uint8_t* payload, size_t payloadSize)>;

	/// Linux-only TCP server: non-blocking epoll, acceptor thread, connection objects (fd, buffers, state),
	/// RX framing (protocol v1 header), TX queue with backpressure, worker pool for packet handling.
	/// EPOLLERR/EPOLLHUP are always handled; never run game/DB logic in the IO thread.
	class NetServer
	{
	public:
		NetServer() = default;
		~NetServer();

		/// Start listening on \a listenPort with \a config. Returns true on success.
		bool Init(uint16_t listenPort, const NetServerConfig& config);

		/// Stop acceptor, close all connections, join workers, release resources.
		void Shutdown();

		/// Enqueue \a packet to send to connection \a connId. Returns false if conn not found or backpressure would be exceeded (connection closed).
		bool Send(uint32_t connId, std::span<const uint8_t> packet);

		/// Set handler called from worker threads for each received packet. Optional; not called if unset.
		void SetPacketHandler(NetServerPacketHandler handler);

		/// Current number of connected clients. Thread-safe.
		uint32_t GetConnectionCount() const;

		/// True after a successful Init() and before Shutdown().
		bool IsRunning() const;

		/// Returns disconnect count for \a reason. Thread-safe (atomics). Only valid while server is running.
		uint64_t GetDisconnectCount(DisconnectReason reason) const;

	private:
		struct Impl;
		Impl* m_impl = nullptr;
	};
}
