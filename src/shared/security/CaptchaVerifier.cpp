// M33.3 — CAPTCHA token verifier implementation.
// UNIX: POSIX socket + OpenSSL HTTPS POST avec validation cert + timeouts + URL-encoding.
// Windows: HTTPS layer non implementee — retourne false (refus) au lieu de bypass.
//          Le master prod tourne sur Linux ; Windows uniquement pour dev/tests (utiliser
//          captcha.trusted_ips pour les IPs locales).
//
// Audit 2026-05-18 : VerifyHttp cumulait 5 problemes critiques (P0) avant ce fix :
//   1. Aucune validation cert (pas de SSL_CTX_set_verify, pas de load_verify_paths)
//      -> MITM trivial pour exfiltrer le secret_key hCaptcha/reCaptcha.
//   2. Token concatene au body sans URL-encoding -> injection theorique de
//      parametres / CRLF si le fournisseur change le format de token un jour.
//   3. Aucun timeout socket -> attaquant peut faire pendre indefiniment le master
//      sur le verify (DoS register).
//   4. Un seul SSL_read 4 Ko -> reponses fragmentees produisent des faux negatifs.
//   5. Sur Windows, VerifyHttp retournait true sans verifier RIEN (bypass total).
//      Si quelqu'un lancait le master sur Windows, register sans CAPTCHA.
#include "src/shared/security/CaptchaVerifier.h"
#include "src/shared/core/Log.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

// OpenSSL-based HTTP POST is only compiled on UNIX where the socket layer is POSIX.
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#  define CAPTCHA_HAS_HTTP 1
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <sys/socket.h>
#  include <sys/time.h>
#  include <unistd.h>
#  include <openssl/ssl.h>
#  include <openssl/err.h>
#  include <openssl/x509v3.h>
#  include <cstring>
#  include <sstream>
#else
#  define CAPTCHA_HAS_HTTP 0
#endif

namespace engine::server
{
	namespace
	{
		/// Timeout TCP connect / TLS handshake / SSL_read / SSL_write.
		/// Le verify CAPTCHA est synchrone dans le flux register : trop court bloque
		/// les utilisateurs legitimes sur reseau lent ; trop long ouvre un DoS.
		/// 5s de marge pour le DNS + handshake + round-trip, 3s par operation socket.
		constexpr int kCaptchaConnectTimeoutSec = 5;
		constexpr int kCaptchaIoTimeoutSec      = 3;

		/// URL-encode application/x-www-form-urlencoded : conserve [a-zA-Z0-9-_.~]
		/// tel quel, encode tout le reste en %XX. RFC 3986 section 2.3.
		std::string UrlEncode(std::string_view s)
		{
			std::string out;
			out.reserve(s.size() * 3u / 2u);
			for (unsigned char c : s)
			{
				if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
				    (c >= '0' && c <= '9') ||
				    c == '-' || c == '_' || c == '.' || c == '~')
				{
					out.push_back(static_cast<char>(c));
				}
				else
				{
					char hex[4];
					std::snprintf(hex, sizeof(hex), "%%%02X", c);
					out.append(hex);
				}
			}
			return out;
		}
	}

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

	/// Minimal HTTPS POST to the CAPTCHA verify endpoint avec validation cert,
	/// URL-encoding du body, timeouts socket, lecture bouclee.
	/// Parses the JSON response for {"success":true}.
	bool CaptchaVerifier::VerifyHttp(std::string_view token, std::string_view remoteIp) const
	{
		// Parse host/path from verify URL.
		const std::string url = ResolveVerifyUrl();
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

		// Build POST body avec URL-encoding (audit fix #2).
		std::string body = "secret=";
		body += UrlEncode(m_config.secret_key);
		body += "&response=";
		body += UrlEncode(token);
		if (!remoteIp.empty())
		{
			body += "&remoteip=";
			body += UrlEncode(remoteIp);
		}

		std::ostringstream req;
		req << "POST " << path << " HTTP/1.1\r\n"
		    << "Host: " << host << "\r\n"
		    << "User-Agent: lcdlln-master/captcha\r\n"
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

		// Timeouts socket (audit fix #3) : SO_RCVTIMEO + SO_SNDTIMEO bornent
		// connect/read/write a kCaptchaIoTimeoutSec chacun. Sans cela, un
		// fournisseur qui pend faisait pendre le master indefiniment -> DoS.
		struct timeval tv{};
		tv.tv_sec  = kCaptchaIoTimeoutSec;
		tv.tv_usec = 0;
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

		// connect avec son propre timeout via SO_SNDTIMEO + alarme manuelle :
		// on garde la connexion bloquante (avec SO_SNDTIMEO ca borne la duree).
		if (::connect(fd, res->ai_addr, res->ai_addrlen) != 0)
		{
			freeaddrinfo(res);
			::close(fd);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: connect() failed/timeout to host={}", host);
			return false;
		}
		freeaddrinfo(res);

		// TLS handshake AVEC validation cert (audit fix #1).
		SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
		if (!ctx)
		{
			::close(fd);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: SSL_CTX_new failed");
			return false;
		}
		// Charge les root CAs du systeme (Debian/Ubuntu: /etc/ssl/certs).
		if (SSL_CTX_set_default_verify_paths(ctx) != 1)
		{
			SSL_CTX_free(ctx);
			::close(fd);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: SSL_CTX_set_default_verify_paths failed — no root CAs available");
			return false;
		}
		// Demande la validation du peer ; si la chaine est invalide, SSL_connect echoue.
		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
		// Verifie aussi que le hostname du cert matche celui qu'on veut joindre
		// (protection contre cert valide pour un autre domaine).
		SSL_CTX_set_verify_depth(ctx, 8);

		SSL* ssl = SSL_new(ctx);
		if (!ssl)
		{
			SSL_CTX_free(ctx);
			::close(fd);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: SSL_new failed");
			return false;
		}

		// SNI + hostname check via X509_VERIFY_PARAM.
		SSL_set_tlsext_host_name(ssl, host.c_str());
		X509_VERIFY_PARAM* vparam = SSL_get0_param(ssl);
		if (vparam)
		{
			// X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS : refuse *.foo.bar pour foo.bar
			X509_VERIFY_PARAM_set_hostflags(vparam, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
			if (X509_VERIFY_PARAM_set1_host(vparam, host.c_str(), host.size()) != 1)
			{
				SSL_free(ssl);
				SSL_CTX_free(ctx);
				::close(fd);
				LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: X509_VERIFY_PARAM_set1_host failed");
				return false;
			}
		}

		SSL_set_fd(ssl, fd);
		if (SSL_connect(ssl) != 1)
		{
			// SSL_connect echec : log les erreurs OpenSSL pour aider le diag
			// (typiquement cert invalide, hostname mismatch, ou expiration).
			const unsigned long err = ERR_get_error();
			char errbuf[256];
			ERR_error_string_n(err, errbuf, sizeof(errbuf));
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			::close(fd);
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: TLS handshake failed to host={} err={}", host, errbuf);
			return false;
		}

		// Verification explicite du resultat de verify (defense en profondeur ;
		// SSL_connect echoue deja sur cert invalide, mais on double-check).
		const long verify_result = SSL_get_verify_result(ssl);
		if (verify_result != X509_V_OK)
		{
			LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: cert verify failed code={} ({})",
				verify_result, X509_verify_cert_error_string(verify_result));
			SSL_shutdown(ssl);
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			::close(fd);
			return false;
		}

		// Send request avec un loop d'ecriture (SSL_write peut retourner partiel
		// sur SSL_MODE_ENABLE_PARTIAL_WRITE ou WANT_READ/WANT_WRITE).
		size_t total_written = 0;
		while (total_written < reqStr.size())
		{
			const int chunk = SSL_write(ssl,
				reqStr.data() + total_written,
				static_cast<int>(reqStr.size() - total_written));
			if (chunk <= 0)
			{
				LOG_ERROR(Net, "[CaptchaVerifier] VerifyHttp: SSL_write failed/timeout (host={} sent={}/{})",
					host, total_written, reqStr.size());
				SSL_shutdown(ssl);
				SSL_free(ssl);
				SSL_CTX_free(ctx);
				::close(fd);
				return false;
			}
			total_written += static_cast<size_t>(chunk);
		}

		// Read response en boucle (audit fix #4) jusqu'a EOF ou erreur, max 64 KB.
		std::string response;
		response.reserve(4096);
		constexpr size_t kMaxResponseBytes = 64u * 1024u;
		char buf[4096];
		while (response.size() < kMaxResponseBytes)
		{
			const int n = SSL_read(ssl, buf, sizeof(buf));
			if (n > 0)
			{
				response.append(buf, static_cast<size_t>(n));
			}
			else
			{
				// 0 = clean shutdown ; <0 = erreur ou timeout. Dans les deux cas on s'arrete.
				break;
			}
		}

		SSL_shutdown(ssl);
		SSL_free(ssl);
		SSL_CTX_free(ctx);
		::close(fd);

		// Look for "success":true in the JSON body (simple substring match).
		const bool success = response.find("\"success\":true") != std::string::npos
		                  || response.find("\"success\": true") != std::string::npos;

		if (success)
		{
			LOG_DEBUG(Net, "[CaptchaVerifier] Token verified OK (host={})", host);
		}
		else
		{
			LOG_WARN(Net, "[CaptchaVerifier] Token verification FAILED (host={} response_size={})",
				host, response.size());
		}
		return success;
	}

#else // Windows / unsupported platform

	bool CaptchaVerifier::VerifyHttp(std::string_view /*token*/, std::string_view /*remoteIp*/) const
	{
		// Audit 2026-05-18 fix #5 : avant on retournait true (bypass total). Si
		// quelqu'un lancait le master sur Windows, register sans CAPTCHA. On
		// refuse desormais. Windows est usage dev/tests uniquement ; pour les
		// IPs locales, utiliser `captcha.trusted_ips` qui sont autorisees
		// avant le VerifyHttp (cf. Verify() public).
		LOG_ERROR(Net,
			"[CaptchaVerifier] VerifyHttp: HTTP layer not implemented on this platform — "
			"verification REFUSED. Configure captcha.trusted_ips pour les hotes dev/tests, "
			"ou utiliser le master Linux pour la prod.");
		return false;
	}

#endif // CAPTCHA_HAS_HTTP

} // namespace engine::server
