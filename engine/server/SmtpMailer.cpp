// M33.2 — SmtpMailer implementation.
// UNIX: minimal SMTP/ESMTP client (plain + STARTTLS via OpenSSL).
// Windows: no-op stub.

#include "engine/server/SmtpMailer.h"
#include "engine/core/Config.h"
#include "engine/core/Log.h"

namespace engine::server
{
	SmtpConfig SmtpConfig::Load(const engine::core::Config& config)
	{
		SmtpConfig cfg;
		cfg.host           = config.GetString("smtp.host", "");
		cfg.port           = static_cast<uint16_t>(config.GetInt("smtp.port", 587));
		cfg.user           = config.GetString("smtp.user", "");
		cfg.password       = config.GetString("smtp.password", "");
		cfg.from_address   = config.GetString("smtp.from", "noreply@game.com");
		cfg.use_starttls   = (config.GetInt("smtp.starttls", 1) != 0);
		cfg.timeout_sec    = static_cast<int>(config.GetInt("smtp.timeout_sec", 10));
		cfg.reset_url_base = config.GetString("smtp.reset_url_base", "https://game.com/reset");
		if (!cfg.host.empty())
			LOG_INFO(Core, "[SmtpMailer] Config loaded (host={}:{} starttls={} from={})",
				cfg.host, cfg.port, cfg.use_starttls ? 1 : 0, cfg.from_address);
		else
			LOG_WARN(Core, "[SmtpMailer] smtp.host not configured — email sending disabled");
		return cfg;
	}
} // namespace engine::server

// ---------------------------------------------------------------------------
// Platform-specific implementation
// ---------------------------------------------------------------------------
#ifdef __unix__

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>

#include <cstring>
#include <string>

namespace
{
	/// Base64-encodes a string using OpenSSL EVP_EncodeBlock.
	static std::string Base64Encode(const std::string& s)
	{
		if (s.empty())
			return {};
		const size_t inLen = s.size();
		// Output length: 4 * ceil(n/3), plus null terminator.
		const size_t outLen = 4u * ((inLen + 2u) / 3u);
		std::string out(outLen + 1u, '\0');
		int len = EVP_EncodeBlock(
			reinterpret_cast<unsigned char*>(out.data()),
			reinterpret_cast<const unsigned char*>(s.data()),
			static_cast<int>(inLen));
		out.resize(static_cast<size_t>(len));
		return out;
	}

	/// RAII wrapper for a TCP + optional TLS socket.
	struct SmtpConn
	{
		int      fd  = -1;
		SSL*     ssl = nullptr;
		SSL_CTX* ctx = nullptr;

		~SmtpConn()
		{
			if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
			if (ctx) { SSL_CTX_free(ctx); ctx = nullptr; }
			if (fd >= 0) { ::close(fd); fd = -1; }
		}

		/// Writes all bytes; returns false on partial/error write.
		bool Write(const std::string& s) const
		{
			const char* p = s.data();
			int remaining = static_cast<int>(s.size());
			while (remaining > 0)
			{
				int n;
				if (ssl)
					n = SSL_write(ssl, p, remaining);
				else
					n = static_cast<int>(::write(fd, p, static_cast<size_t>(remaining)));
				if (n <= 0)
					return false;
				p += n;
				remaining -= n;
			}
			return true;
		}

		/// Reads one character; returns -1 on error.
		int ReadChar() const
		{
			char c = '\0';
			int n;
			if (ssl)
				n = SSL_read(ssl, &c, 1);
			else
				n = static_cast<int>(::read(fd, &c, 1));
			if (n <= 0)
				return -1;
			return static_cast<unsigned char>(c);
		}

		/// Reads until '\n' (inclusive). Returns empty string on error.
		std::string ReadLine() const
		{
			std::string line;
			line.reserve(128);
			for (;;)
			{
				int c = ReadChar();
				if (c < 0)
					break;
				line += static_cast<char>(c);
				if (c == '\n')
					break;
			}
			return line;
		}

		/// Reads a (possibly multi-line) SMTP response; returns the numeric code, -1 on error.
		/// Sets lastLine to the final line of the response.
		int ReadResponse(std::string& lastLine) const
		{
			int code = -1;
			for (;;)
			{
				std::string line = ReadLine();
				if (line.size() < 4u)
					return -1;
				code = std::atoi(line.c_str());
				// A space in position 3 signals the last line of a multi-line response.
				if (line[3] == ' ')
				{
					lastLine = std::move(line);
					break;
				}
				// '-' in position 3 means more lines follow; keep reading.
			}
			return code;
		}

		/// Upgrades plain TCP to TLS using OpenSSL STARTTLS (called after server says 220 go ahead).
		bool UpgradeToTLS()
		{
			ctx = SSL_CTX_new(TLS_client_method());
			if (!ctx)
				return false;
			ssl = SSL_new(ctx);
			if (!ssl)
				return false;
			SSL_set_fd(ssl, fd);
			if (SSL_connect(ssl) <= 0)
			{
				ERR_clear_error();
				return false;
			}
			return true;
		}
	};
} // anonymous namespace

namespace engine::server
{
	bool SmtpMailer::Send(const SmtpConfig& cfg,
	                      const std::string& to,
	                      const std::string& subject,
	                      const std::string& body)
	{
		if (cfg.host.empty())
		{
			LOG_WARN(Net, "[SmtpMailer] Send skipped: smtp.host not configured (to={})", to);
			return false;
		}

		// Resolve + connect
		struct addrinfo hints{};
		hints.ai_family   = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		const std::string portStr = std::to_string(cfg.port);
		struct addrinfo* res = nullptr;
		if (::getaddrinfo(cfg.host.c_str(), portStr.c_str(), &hints, &res) != 0 || res == nullptr)
		{
			LOG_ERROR(Net, "[SmtpMailer] getaddrinfo failed for {}:{}", cfg.host, cfg.port);
			return false;
		}

		SmtpConn conn;
		conn.fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (conn.fd < 0)
		{
			::freeaddrinfo(res);
			LOG_ERROR(Net, "[SmtpMailer] socket() failed");
			return false;
		}

		// Apply read/write timeouts.
		struct timeval tv{};
		tv.tv_sec = static_cast<time_t>(cfg.timeout_sec);
		::setsockopt(conn.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		::setsockopt(conn.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

		if (::connect(conn.fd, res->ai_addr, res->ai_addrlen) != 0)
		{
			::freeaddrinfo(res);
			LOG_ERROR(Net, "[SmtpMailer] connect() to {}:{} failed", cfg.host, cfg.port);
			return false;
		}
		::freeaddrinfo(res);
		LOG_DEBUG(Net, "[SmtpMailer] TCP connected to {}:{}", cfg.host, cfg.port);

		std::string resp;
		int code;

		// Read server banner (220)
		code = conn.ReadResponse(resp);
		if (code != 220)
		{
			LOG_ERROR(Net, "[SmtpMailer] Unexpected banner code={} ({})", code, resp);
			return false;
		}

		// EHLO
		if (!conn.Write("EHLO localhost\r\n"))
		{
			LOG_ERROR(Net, "[SmtpMailer] Write EHLO failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 250)
		{
			LOG_ERROR(Net, "[SmtpMailer] EHLO rejected code={}", code);
			return false;
		}

		// Optional STARTTLS upgrade
		if (cfg.use_starttls)
		{
			if (!conn.Write("STARTTLS\r\n"))
			{
				LOG_ERROR(Net, "[SmtpMailer] Write STARTTLS failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 220)
			{
				LOG_ERROR(Net, "[SmtpMailer] STARTTLS rejected code={}", code);
				return false;
			}
			if (!conn.UpgradeToTLS())
			{
				LOG_ERROR(Net, "[SmtpMailer] TLS handshake failed");
				return false;
			}
			// Re-EHLO after STARTTLS
			if (!conn.Write("EHLO localhost\r\n"))
			{
				LOG_ERROR(Net, "[SmtpMailer] Write EHLO(2) failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 250)
			{
				LOG_ERROR(Net, "[SmtpMailer] EHLO(2) rejected code={}", code);
				return false;
			}
			LOG_DEBUG(Net, "[SmtpMailer] STARTTLS + EHLO OK");
		}

		// AUTH LOGIN (if credentials provided)
		if (!cfg.user.empty())
		{
			if (!conn.Write("AUTH LOGIN\r\n"))
			{
				LOG_ERROR(Net, "[SmtpMailer] Write AUTH LOGIN failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 334)
			{
				LOG_ERROR(Net, "[SmtpMailer] AUTH LOGIN rejected code={}", code);
				return false;
			}
			if (!conn.Write(Base64Encode(cfg.user) + "\r\n"))
			{
				LOG_ERROR(Net, "[SmtpMailer] Write AUTH user failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 334)
			{
				LOG_ERROR(Net, "[SmtpMailer] AUTH user rejected code={}", code);
				return false;
			}
			if (!conn.Write(Base64Encode(cfg.password) + "\r\n"))
			{
				LOG_ERROR(Net, "[SmtpMailer] Write AUTH password failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 235)
			{
				LOG_ERROR(Net, "[SmtpMailer] AUTH credentials rejected code={}", code);
				return false;
			}
			LOG_DEBUG(Net, "[SmtpMailer] AUTH LOGIN OK");
		}

		// MAIL FROM
		if (!conn.Write("MAIL FROM:<" + cfg.from_address + ">\r\n"))
		{
			LOG_ERROR(Net, "[SmtpMailer] Write MAIL FROM failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 250)
		{
			LOG_ERROR(Net, "[SmtpMailer] MAIL FROM rejected code={}", code);
			return false;
		}

		// RCPT TO
		if (!conn.Write("RCPT TO:<" + to + ">\r\n"))
		{
			LOG_ERROR(Net, "[SmtpMailer] Write RCPT TO failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 250)
		{
			LOG_ERROR(Net, "[SmtpMailer] RCPT TO rejected code={}", code);
			return false;
		}

		// DATA
		if (!conn.Write("DATA\r\n"))
		{
			LOG_ERROR(Net, "[SmtpMailer] Write DATA failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 354)
		{
			LOG_ERROR(Net, "[SmtpMailer] DATA rejected code={}", code);
			return false;
		}

		// Build and send message headers + body + end-of-data marker.
		std::string msg;
		msg.reserve(512u + body.size());
		msg += "From: " + cfg.from_address + "\r\n";
		msg += "To: " + to + "\r\n";
		msg += "Subject: " + subject + "\r\n";
		msg += "MIME-Version: 1.0\r\n";
		msg += "Content-Type: text/plain; charset=UTF-8\r\n";
		msg += "\r\n";
		msg += body;
		msg += "\r\n.\r\n";

		if (!conn.Write(msg))
		{
			LOG_ERROR(Net, "[SmtpMailer] Write message body failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 250)
		{
			LOG_ERROR(Net, "[SmtpMailer] Message rejected by server code={}", code);
			return false;
		}

		// QUIT (best-effort; ignore response)
		conn.Write("QUIT\r\n");

		LOG_INFO(Net, "[SmtpMailer] Email sent OK (to={} subject={})", to, subject);
		return true;
	}
} // namespace engine::server

#else // !__unix__ — Windows stub

namespace engine::server
{
	bool SmtpMailer::Send(const SmtpConfig& /*cfg*/,
	                      const std::string& to,
	                      const std::string& subject,
	                      const std::string& /*body*/)
	{
		LOG_WARN(Net, "[SmtpMailer] SMTP not implemented on Win32 — email not sent (to={} subject={})", to, subject);
		return false;
	}
} // namespace engine::server

#endif // __unix__
