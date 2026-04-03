#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace engine::server
{
	/// Minimal HTTP health/readiness server (M23.1). UNIX only.
	/// Serves GET /healthz (200 OK if process alive) and GET /readyz (200 OK if ready callback returns true).
	/// Runs on a separate port and bind address (config: server.health.port, server.health.bind).
	class HealthEndpoint
	{
	public:
		HealthEndpoint() = default;
		~HealthEndpoint();

		HealthEndpoint(const HealthEndpoint&) = delete;
		HealthEndpoint& operator=(const HealthEndpoint&) = delete;

		/// Starts the health HTTP server thread. Bind address and port are from config or defaults.
		/// \param bindAddress IPv4 address to bind (e.g. "127.0.0.1", "0.0.0.0").
		/// \param port TCP port for the health server (separate from game port).
		/// \param readyCheck Called for GET /readyz; return true if ready (DB OK, migrations OK, etc.).
	/// \param metricsProvider Optional. Called for GET /metrics; returns Prometheus text (M23.2). If null, /metrics returns 404.
	/// \param statusProvider Optional. Called for GET /status; returns JSON body. If null, /status returns 404.
	/// \param webPortalStatusHtmlProvider Optional. Called for GET /web-portal/status; returns HTML page. If null, returns 404.
		/// \return true if listen socket created and thread started; false on error.
		bool Init(uint16_t port, const std::string& bindAddress, std::function<bool()> readyCheck,
		std::function<std::string()> metricsProvider = nullptr,
		std::function<std::string()> statusProvider = nullptr,
		std::function<std::string()> webPortalStatusHtmlProvider = nullptr);

		/// Stops the server thread and closes the listen socket. Safe to call multiple times.
		void Shutdown();

		/// Returns true if Init() succeeded and Shutdown() has not been called.
		bool IsRunning() const { return m_running.load(std::memory_order_relaxed); }

	private:
		void ThreadRun();

		std::function<bool()> m_readyCheck;
		std::function<std::string()> m_metricsProvider;
		std::function<std::string()> m_statusProvider;
		std::function<std::string()> m_webPortalStatusHtmlProvider;
		std::atomic<bool> m_running{ false };
		std::thread m_thread;
		std::atomic<bool> m_lastReady{ false };
		int m_listenFd = -1;
		int m_wakePipeRead = -1;
		int m_wakePipeWrite = -1;
	};
}
