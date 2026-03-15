// M23.1 — Minimal HTTP health/readiness endpoint. UNIX only.

#include "engine/server/HealthEndpoint.h"
#include "engine/core/Log.h"

#if defined(__unix__) || defined(__linux__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#endif

namespace engine::server
{
#if defined(__unix__) || defined(__linux__)

	namespace
	{
		constexpr size_t kRequestBufSize = 512;
		constexpr int kListenBacklog = 4;

		void CloseFd(int& fd)
		{
			if (fd >= 0)
			{
				::close(fd);
				fd = -1;
			}
		}

		bool ParseRequestLine(const char* line, size_t len, bool& outHealthz, bool& outReadyz, bool& outMetrics)
		{
			outHealthz = false;
			outReadyz = false;
			outMetrics = false;
			if (len < 14)
				return false;
			if (std::strncmp(line, "GET ", 4) != 0)
				return false;
			const char* path = line + 4;
			size_t pathLen = 0;
			while (pathLen < len - 4 && path[pathLen] != ' ' && path[pathLen] != '\r' && path[pathLen] != '\n')
				++pathLen;
			if (pathLen == 8 && std::strncmp(path, "/healthz", 8) == 0)
			{
				outHealthz = true;
				return true;
			}
			if (pathLen == 7 && std::strncmp(path, "/readyz", 7) == 0)
			{
				outReadyz = true;
				return true;
			}
			if (pathLen == 8 && std::strncmp(path, "/metrics", 8) == 0)
			{
				outMetrics = true;
				return true;
			}
			return false;
		}

		void SendResponse(int fd, int statusCode, const char* statusText, const char* jsonBody)
		{
			size_t bodyLen = std::strlen(jsonBody);
			std::string header;
			header.reserve(128);
			header += "HTTP/1.1 ";
			header += std::to_string(statusCode);
			header += " ";
			header += statusText;
			header += "\r\nContent-Type: application/json\r\nContent-Length: ";
			header += std::to_string(bodyLen);
			header += "\r\nConnection: close\r\n\r\n";
			::send(fd, header.data(), header.size(), MSG_NOSIGNAL);
			::send(fd, jsonBody, bodyLen, MSG_NOSIGNAL);
		}

		void SendMetricsResponse(int fd, const std::string& body)
		{
			std::string header;
			header.reserve(192);
			header += "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: ";
			header += std::to_string(body.size());
			header += "\r\nConnection: close\r\n\r\n";
			::send(fd, header.data(), header.size(), MSG_NOSIGNAL);
			if (!body.empty())
				::send(fd, body.data(), body.size(), MSG_NOSIGNAL);
		}
	}

	HealthEndpoint::~HealthEndpoint()
	{
		Shutdown();
	}

	bool HealthEndpoint::Init(uint16_t port, const std::string& bindAddress, std::function<bool()> readyCheck,
		std::function<std::string()> metricsProvider)
	{
		if (m_running.load(std::memory_order_relaxed))
		{
			LOG_WARN(Core, "[HealthEndpoint] Init ignored: already running");
			return true;
		}

		m_readyCheck = std::move(readyCheck);
		m_metricsProvider = std::move(metricsProvider);

		m_listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
		if (m_listenFd < 0)
		{
			LOG_ERROR(Core, "[HealthEndpoint] Init FAILED: socket() failed");
			return false;
		}

		int opt = 1;
		if (::setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		{
			LOG_WARN(Core, "[HealthEndpoint] setsockopt SO_REUSEADDR failed");
		}

		struct sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		if (bindAddress.empty() || bindAddress == "0.0.0.0")
		{
			addr.sin_addr.s_addr = INADDR_ANY;
		}
		else
		{
			if (::inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) <= 0)
			{
				LOG_ERROR(Core, "[HealthEndpoint] Init FAILED: invalid bind address '{}'", bindAddress);
				CloseFd(m_listenFd);
				return false;
			}
		}

		if (::bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
		{
			LOG_ERROR(Core, "[HealthEndpoint] Init FAILED: bind({}, {}) failed", bindAddress, port);
			CloseFd(m_listenFd);
			return false;
		}

		if (::listen(m_listenFd, kListenBacklog) < 0)
		{
			LOG_ERROR(Core, "[HealthEndpoint] Init FAILED: listen() failed");
			CloseFd(m_listenFd);
			return false;
		}

		int pipeFds[2];
		if (::pipe(pipeFds) < 0)
		{
			LOG_ERROR(Core, "[HealthEndpoint] Init FAILED: pipe() failed");
			CloseFd(m_listenFd);
			return false;
		}
		m_wakePipeRead = pipeFds[0];
		m_wakePipeWrite = pipeFds[1];

		m_running.store(true);
		m_thread = std::thread(&HealthEndpoint::ThreadRun, this);
		LOG_INFO(Core, "[HealthEndpoint] Init OK (bind={}, port={})", bindAddress, port);
		return true;
	}

	void HealthEndpoint::Shutdown()
	{
		if (!m_running.exchange(false))
			return;
		if (m_wakePipeWrite >= 0)
		{
			char c = 0;
			::write(m_wakePipeWrite, &c, 1);
		}
		if (m_thread.joinable())
			m_thread.join();
		CloseFd(m_listenFd);
		CloseFd(m_wakePipeRead);
		CloseFd(m_wakePipeWrite);
		LOG_INFO(Core, "[HealthEndpoint] Shutdown OK");
	}

	void HealthEndpoint::ThreadRun()
	{
		struct pollfd fds[2];
		fds[0].fd = m_listenFd;
		fds[0].events = POLLIN;
		fds[1].fd = m_wakePipeRead;
		fds[1].events = POLLIN;

		while (m_running.load(std::memory_order_relaxed))
		{
			int r = ::poll(fds, 2, 1000);
			if (r < 0)
			{
				if (m_running.load(std::memory_order_relaxed))
					LOG_WARN(Core, "[HealthEndpoint] poll failed");
				break;
			}
			if (r == 0)
				continue;
			if (fds[1].revents & POLLIN)
				break;

			if (!(fds[0].revents & POLLIN))
				continue;

			struct sockaddr_in peer{};
			socklen_t peerLen = sizeof(peer);
			int clientFd = ::accept(m_listenFd, reinterpret_cast<struct sockaddr*>(&peer), &peerLen);
			if (clientFd < 0)
				continue;

			char buf[kRequestBufSize];
			ssize_t n = ::recv(clientFd, buf, sizeof(buf) - 1, 0);
			if (n <= 0)
			{
				::close(clientFd);
				continue;
			}
			buf[n] = '\0';

			bool healthz = false;
			bool readyz = false;
			bool metrics = false;
			bool valid = ParseRequestLine(buf, static_cast<size_t>(n), healthz, readyz, metrics);

			if (valid && metrics)
			{
				if (m_metricsProvider)
				{
					std::string body = m_metricsProvider();
					SendMetricsResponse(clientFd, body);
				}
				else
					SendResponse(clientFd, 404, "Not Found", "{\"status\":\"error\",\"reason\":\"metrics not configured\"}");
			}
			else if (valid && healthz)
			{
				SendResponse(clientFd, 200, "OK", "{\"status\":\"ok\"}");
			}
			else if (valid && readyz)
			{
				bool ready = m_readyCheck ? m_readyCheck() : false;
				if (ready)
				{
					bool wasNotReady = !m_lastReady.exchange(true);
					if (wasNotReady)
						LOG_INFO(Core, "[HealthEndpoint] readiness transition: not-ready -> ready");
					SendResponse(clientFd, 200, "OK", "{\"status\":\"ok\"}");
				}
				else
				{
					m_lastReady.store(false);
					SendResponse(clientFd, 503, "Service Unavailable", "{\"status\":\"not_ready\",\"reason\":\"db\"}");
				}
			}
			else
			{
				SendResponse(clientFd, 404, "Not Found", "{\"status\":\"error\",\"reason\":\"unknown path\"}");
			}

			::close(clientFd);
		}
	}

#else

	HealthEndpoint::~HealthEndpoint() = default;

	bool HealthEndpoint::Init(uint16_t /*port*/, const std::string& /*bindAddress*/, std::function<bool()> /*readyCheck*/,
		std::function<std::string()> /*metricsProvider*/)
	{
		LOG_WARN(Core, "[HealthEndpoint] Init skipped: UNIX only");
		return false;
	}

	void HealthEndpoint::Shutdown() {}

#endif
}
