// M33.3 — CAPTCHA token verifier implementation.
// UNIX: uses blocking POSIX socket + OpenSSL to POST to the provider verify endpoint.
// Windows: HTTP layer not implemented — logs warning, returns true (bypass for local/dev builds).
#include "engine/server/CaptchaVerifier.h"
#include "engine/core/Log.h"

#include <algorithm>
#include <string>
#include <string_view>

// OpenSSL-based HTTP POST is only compiled on UNIX where OpenSSL is available.
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#  define CAPTCHA_HAS_HTTP 1
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#  include <cstring>
#  include <sstream>
#else
#  define CAPTCHA_HAS_HTTP 0
#endif

namespace engine::server
{
	// -----------------------------------------------------------------------
	// Configuration
	// -----------------------------------------------------------------------

	void CaptchaVerifier::SetConfig(const CaptchaConfig& config)
	{
		m_config = config;
		if (m_config.enabled)
		{
			LOG_INFO(Net, "[CaptchaVerifier] Init OK (provider={} secret_key_set={} trusted_ips={})",
				m_config.provider == CaptchaProvider::HCaptcha ? "hcaptcha" : "recaptcha",
				!m_config.secret_key.empty(),
				static_cast<uint32_t>(m_config.trusted_ips.size()));
		}
		else
		{
			LOG_WARN(Net, "[CaptchaVerifier] CAPTCHA disabled (bypass mode) — do not use in production");
		}
	}

	CaptchaConfig CaptchaVerifier::LoadConfig(const engine::core::Config& config)
	{
		CaptchaConfig c;
		c.enabled    = (config.GetInt("captcha.enabled", 0) != 0);
		c.secret_key = config.GetString("captcha.secret_key", "");

		const std::string provider = config.GetString("captcha.provider", "hcaptcha");
		if (provider == "recaptcha")
			c.provider = CaptchaProvider::ReCaptcha;
		else
			c.provider = CaptchaProvider::HCaptcha;

		c.verify_url_override = config.GetString("captcha.verify_url_override", "");

		// Parse comma-separated trusted IPs.
		const std::string raw = config.GetString("captcha.trusted_ips", "");
		if (!raw.empty())
		{
			std::string token;
			for (char ch : raw)
			{
				if (ch == ',')
				{
					if (!token.empty())
					{
						c.trusted_ips.push_back(token);
						token.clear();
					}
				}
				else
				{
					token += ch;
				}
			}
			if (!token.empty())
				c.trusted_ips.push_back(token);
		}
		return c;
	}

	// -----------------------------------------------------------------------
	// Public interface
	// -----------------------------------------------------------------------

	bool CaptchaVerifier::Verify(std::string_view token, std::string_view remoteIp) const
	{
		if (!m_config.enabled)
		{
			LOG_DEBUG(Net, "[CaptchaVerifier] Bypass mode — token accepted without verification");
			return true;
		}
		if (token.empty())
		{
			LOG_WARN(Net, "[CaptchaVerifier] Verify FAILED: empty token (ip={})", remoteIp);
			return false;
		}
		if (IsTrustedIp(remoteIp))
		{
			LOG_DEBUG(Net, "[CaptchaVerifier] Trusted IP bypass: ip={}", remoteIp);
			return true;
		}
		if (m_config.secret_key.empty())
		{
			LOG_WARN(Net, "[CaptchaVerifier] Verify FAILED: no secret_key configured");
			return false;
		}
		return VerifyHttp(token, remoteIp);
	}

	// -----------------------------------------------------------------------
	// Private helpers
	// -----------------------------------------------------------------------

	bool CaptchaVerifier::IsTrustedIp(std::string_view ip) const
	{
		const std::string ipStr(ip);
		for (const auto& trusted : m_config.trusted_ips)
		{
			if (trusted == ipStr)
				return true;
		}
		return false;
	}

	std::string CaptchaVerifier::ResolveVerifyUrl() const
	{
		if (!m_config.verify_url_override.empty())
			return m_config.verify_url_override;
		if (m_config.provider == CaptchaProvider::HCaptcha)
			return "https://hcaptcha.com/siteverify";
		return "https://www.google.com/recaptcha/api/siteverify";
	}

#if CAPTCHA_HAS_HTTP

	/// Minimal HTTPS POST to the CAPTCHA verify endpoint.
	/// Parses the JSON response for {"success":true}.
	bool CaptchaVerifier::VerifyHttp(std::string_view token, std::string_view remoteIp) const
	{
		// Parse host/path from verify URL.
		const std::string url = ResolveVerifyUrl();
		// Expected format: https://<host>/<path>
		if (url.rfind("https://", 0) != 0)
		{
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: unsupported URL scheme: {}", url);
			return false;
		}
		const std::string hostAndPath = url.substr(8); // strip "https://"
		const auto slashPos = hostAndPath.find('/');
		const std::string host = (slashPos == std::string::npos)
			? hostAndPath
			: hostAndPath.substr(0, slashPos);
		const std::string path = (slashPos == std::string::npos)
			? "/"
			: hostAndPath.substr(slashPos);

		// Build POST body.
		std::string body = "secret=";
		body += m_config.secret_key;
		body += "&response=";
		body += std::string(token);
		if (!remoteIp.empty())
		{
			body += "&remoteip=";
			body += std::string(remoteIp);
		}

		// Build HTTP request.
		std::ostringstream req;
		req << "POST " << path << " HTTP/1.1\r\n"
		    << "Host: " << host << "\r\n"
		    << "Content-Type: application/x-www-form-urlencoded\r\n"
		    << "Content-Length: " << body.size() << "\r\n"
		    << "Connection: close\r\n"
		    << "\r\n"
		    << body;
		const std::string reqStr = req.str();

		// Resolve host.
		struct addrinfo hints{};
		hints.ai_family   = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		struct addrinfo* res = nullptr;
		if (getaddrinfo(host.c_str(), "443", &hints, &res) != 0 || res == nullptr)
		{
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: DNS resolution failed for host={}", host);
			return false;
		}

		// Connect socket.
		int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0)
		{
			freeaddrinfo(res);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: socket() failed");
			return false;
		}
		if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0)
		{
			freeaddrinfo(res);
			::close(fd);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: connect() failed to host={}", host);
			return false;
		}
		freeaddrinfo(res);

		// TLS handshake.
		SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
		if (!ctx)
		{
			::close(fd);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: SSL_CTX_new failed");
			return false;
		}
		SSL* ssl = SSL_new(ctx);
		SSL_set_fd(ssl, fd);
		SSL_set_tlsext_host_name(ssl, host.c_str());
		if (SSL_connect(ssl) != 1)
		{
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			::close(fd);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: TLS handshake failed to host={}", host);
			return false;
		}

		// Send request.
		SSL_write(ssl, reqStr.data(), static_cast<int>(reqStr.size()));

		// Read response (up to 4 KB).
		char buf[4096]{};
		int  read_len = SSL_read(ssl, buf, sizeof(buf) - 1);
		buf[read_len > 0 ? read_len : 0] = '\0';

		SSL_shutdown(ssl);
		SSL_free(ssl);
		SSL_CTX_free(ctx);
		::close(fd);

		// Look for "success":true in the JSON body (simple substring match).
		const std::string response(buf, read_len > 0 ? static_cast<size_t>(read_len) : 0u);
		const bool success = response.find("\"success\":true") != std::string::npos
		                  || response.find("\"success\": true") != std::string::npos;

		if (success)
		{
			LOG_DEBUG(Net, "[CaptchaVerifier] Token verified OK (host={})", host);
		}
		else
		{
			LOG_WARN(Net, "[CaptchaVerifier] Token verification FAILED (host={})", host);
		}
		return success;
	}

#else // Windows / unsupported platform

	bool CaptchaVerifier::VerifyHttp(std::string_view /*token*/, std::string_view /*remoteIp*/) const
	{
		LOG_WARN(Net,
			"[CaptchaVerifier] VerifyHttp: HTTP layer not implemented on this platform — "
			"token accepted (bypass). Configure an external CAPTCHA proxy for production.");
		return true;
	}

#endif // CAPTCHA_HAS_HTTP

} // namespace engine::server
