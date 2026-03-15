#pragma once

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace engine::server
{
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

	private:
		struct Impl;
		Impl* m_impl = nullptr;
	};
}
