// M33.2 — SmtpMailer implementation.
// UNIX: minimal SMTP/ESMTP client (plain + STARTTLS via OpenSSL).
// Windows: no-op stub.

#include "src/masterd/email/SmtpMailer.h"
#include "src/shared/core/Config.h"
#include "src/shared/core/Log.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace
{
	/// Affichage journaux sans exposer la partie locale de l’adresse.
	std::string RedactEmailForLog(std::string_view to)
	{
		const size_t at = to.find('@');
		if (at == std::string::npos || at == 0)
			return "<adresse>";
		return std::string("<***@") + std::string(to.substr(at + 1)) + ">";
	}
} // namespace

namespace engine::server
{
	namespace
	{
		/// Résout \a relative à côté du fichier de config principal (même dossier que \c config.json effectif).
		/// Important : si \a primaryConfigPath est seulement \c "config.json" (sans « dossier/ »),
		/// l’ancienne logique retombait sur un chemin SMTP relatif au seul CWD ; on utilise désormais
		/// \c absolute(primary).parent_path() pour coller au répertoire réel du JSON chargé (ex. \c /app sous Docker).
		static std::string ResolveSmtpSecretsPath(std::string_view primaryConfigPath, std::string_view relative)
		{
			namespace fs = std::filesystem;
			fs::path rel(relative);
			if (rel.is_absolute())
				return rel.lexically_normal().string();
			const fs::path primaryAbs = fs::absolute(fs::path(primaryConfigPath));
			fs::path configDir = primaryAbs.parent_path();
			if (configDir.empty())
				configDir = fs::current_path();
			return (configDir / rel).lexically_normal().string();
		}
	} // namespace

	SmtpConfig SmtpConfig::Load(const engine::core::Config& mainConfig, std::string_view primaryConfigPath)
	{
		SmtpConfig cfg;
		const std::string smtpFileRel = mainConfig.GetString("smtp.config_file", "smtp.local.json");
		const std::string resolved    = ResolveSmtpSecretsPath(primaryConfigPath, smtpFileRel);

		engine::core::Config secrets;
		if (!secrets.LoadFromFile(resolved))
		{
			LOG_WARN(Smtp, "[SmtpMailer] SMTP secrets file missing or unreadable (path={}) — email disabled", resolved);
			return cfg;
		}

		cfg.host           = secrets.GetString("smtp.host", "");
		cfg.port           = static_cast<uint16_t>(secrets.GetInt("smtp.port", 0));
		cfg.user           = secrets.GetString("smtp.user", "");
		cfg.password       = secrets.GetString("smtp.password", "");
		cfg.from_address   = secrets.GetString("smtp.from", "");
		cfg.use_starttls   = secrets.Has("smtp.starttls") ? (secrets.GetInt("smtp.starttls", 0) != 0) : true;
		cfg.timeout_sec    = static_cast<int>(secrets.GetInt("smtp.timeout_sec", 10));
		cfg.reset_url_base = secrets.GetString("smtp.reset_url_base", "");

		if (cfg.host.empty())
		{
			LOG_WARN(Smtp, "[SmtpMailer] smtp.host empty in {} — email disabled", resolved);
			return cfg;
		}
		if (cfg.port == 0)
		{
			LOG_ERROR(Smtp, "[SmtpMailer] smtp.port missing or zero in {} — email disabled", resolved);
			cfg = SmtpConfig{};
			return cfg;
		}
		if (cfg.from_address.empty())
			LOG_WARN(Smtp, "[SmtpMailer] smtp.from empty in {} — sending may fail", resolved);
		if (cfg.reset_url_base.empty())
			LOG_WARN(Smtp, "[SmtpMailer] smtp.reset_url_base empty in {} — reset links may be invalid", resolved);

		LOG_INFO(Smtp, "[SmtpMailer] Config loaded from sidecar (path={} host={}:{} starttls={} from={})",
			resolved, cfg.host, cfg.port, cfg.use_starttls ? 1 : 0, cfg.from_address);
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

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>

namespace
{
	static std::string TrimSmtpResponse(const std::string& s)
	{
		std::string out;
		out.reserve(std::min<size_t>(s.size(), 240u));
		for (char c : s)
		{
			if (c == '\r' || c == '\n')
				out.push_back(' ');
			else
				out.push_back(c);
			if (out.size() >= 220u)
				break;
		}
		return out;
	}

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
	                      const std::string& body,
	                      bool isHtml)
	{
		if (cfg.host.empty())
		{
			LOG_WARN(Smtp, "[SmtpMailer] Send skipped: smtp.host not configured (to={})", RedactEmailForLog(to));
			return false;
		}

		LOG_WARN(Smtp,
			"[SMTP] session démarrée → {} host={}:{} starttls={} auth={} (détail niveau Info, sous-système Smtp)",
			RedactEmailForLog(to),
			cfg.host,
			static_cast<unsigned>(cfg.port),
			cfg.use_starttls ? 1 : 0,
			cfg.user.empty() ? 0 : 1);
		LOG_INFO(Smtp,
			"[SmtpMailer] envoi démarré to={} subject_len={} host={}:{} starttls={} auth={} timeout_sec={}",
			RedactEmailForLog(to),
			static_cast<int>(subject.size()),
			cfg.host,
			cfg.port,
			cfg.use_starttls ? 1 : 0,
			cfg.user.empty() ? 0 : 1,
			cfg.timeout_sec);

		// Resolve + connect
		struct addrinfo hints{};
		hints.ai_family   = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		const std::string portStr = std::to_string(cfg.port);
		struct addrinfo* res = nullptr;
		const int gai = ::getaddrinfo(cfg.host.c_str(), portStr.c_str(), &hints, &res);
		if (gai != 0 || res == nullptr)
		{
			LOG_ERROR(Smtp, "[SmtpMailer] getaddrinfo échoué host={}:{} ({})", cfg.host, cfg.port, gai != 0 ? gai_strerror(gai) : "null");
			return false;
		}

		SmtpConn conn;
		conn.fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (conn.fd < 0)
		{
			::freeaddrinfo(res);
			LOG_ERROR(Smtp, "[SmtpMailer] socket() échoué errno={}", errno);
			return false;
		}

		// Apply read/write timeouts.
		struct timeval tv{};
		tv.tv_sec = static_cast<time_t>(cfg.timeout_sec);
		::setsockopt(conn.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		::setsockopt(conn.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

		if (::connect(conn.fd, res->ai_addr, res->ai_addrlen) != 0)
		{
			const int e = errno;
			::freeaddrinfo(res);
			LOG_ERROR(Smtp, "[SmtpMailer] connect() échoué vers {}:{} errno={} ({})", cfg.host, cfg.port, e, std::strerror(e));
			return false;
		}
		::freeaddrinfo(res);
		LOG_INFO(Smtp, "[SmtpMailer] TCP connecté à {}:{}", cfg.host, cfg.port);

		std::string resp;
		int code;

		// Read server banner (220)
		code = conn.ReadResponse(resp);
		if (code != 220)
		{
			LOG_ERROR(Smtp, "[SmtpMailer] bannière SMTP inattendue code={} réponse='{}'", code, TrimSmtpResponse(resp));
			return false;
		}
		LOG_INFO(Smtp, "[SmtpMailer] bannière OK réponse='{}'", TrimSmtpResponse(resp));

		// EHLO
		if (!conn.Write("EHLO localhost\r\n"))
		{
			LOG_ERROR(Smtp, "[SmtpMailer] Write EHLO failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 250)
		{
			LOG_ERROR(Smtp, "[SmtpMailer] EHLO refusé code={} réponse='{}'", code, TrimSmtpResponse(resp));
			return false;
		}

		// Optional STARTTLS upgrade
		if (cfg.use_starttls)
		{
			if (!conn.Write("STARTTLS\r\n"))
			{
				LOG_ERROR(Smtp, "[SmtpMailer] Write STARTTLS failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 220)
			{
				LOG_ERROR(Smtp, "[SmtpMailer] STARTTLS refusé code={} réponse='{}'", code, TrimSmtpResponse(resp));
				return false;
			}
			if (!conn.UpgradeToTLS())
			{
				LOG_ERROR(Smtp, "[SmtpMailer] échec handshake TLS (après STARTTLS)");
				return false;
			}
			// Re-EHLO after STARTTLS
			if (!conn.Write("EHLO localhost\r\n"))
			{
				LOG_ERROR(Smtp, "[SmtpMailer] Write EHLO(2) failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 250)
			{
				LOG_ERROR(Smtp, "[SmtpMailer] EHLO(2) refusé code={} réponse='{}'", code, TrimSmtpResponse(resp));
				return false;
			}
			LOG_INFO(Smtp, "[SmtpMailer] STARTTLS + EHLO post-TLS OK");
		}

		// AUTH LOGIN (if credentials provided)
		if (!cfg.user.empty())
		{
			LOG_INFO(Smtp, "[SmtpMailer] AUTH LOGIN (utilisateur non vide)");
			if (!conn.Write("AUTH LOGIN\r\n"))
			{
				LOG_ERROR(Smtp, "[SmtpMailer] Write AUTH LOGIN failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 334)
			{
				LOG_ERROR(Smtp, "[SmtpMailer] AUTH LOGIN refusé code={} réponse='{}'", code, TrimSmtpResponse(resp));
				return false;
			}
			if (!conn.Write(Base64Encode(cfg.user) + "\r\n"))
			{
				LOG_ERROR(Smtp, "[SmtpMailer] Write AUTH user failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 334)
			{
				LOG_ERROR(Smtp, "[SmtpMailer] AUTH user refusé code={} réponse='{}'", code, TrimSmtpResponse(resp));
				return false;
			}
			if (!conn.Write(Base64Encode(cfg.password) + "\r\n"))
			{
				LOG_ERROR(Smtp, "[SmtpMailer] Write AUTH password failed");
				return false;
			}
			code = conn.ReadResponse(resp);
			if (code != 235)
			{
				LOG_ERROR(Smtp, "[SmtpMailer] AUTH mot de passe refusé code={} réponse='{}'", code, TrimSmtpResponse(resp));
				return false;
			}
			LOG_INFO(Smtp, "[SmtpMailer] AUTH LOGIN OK");
		}
		else
		{
			LOG_INFO(Smtp, "[SmtpMailer] pas d'AUTH (smtp.user vide)");
		}

		// MAIL FROM
		if (!conn.Write("MAIL FROM:<" + cfg.from_address + ">\r\n"))
		{
			LOG_ERROR(Smtp, "[SmtpMailer] Write MAIL FROM failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 250)
		{
			LOG_ERROR(Smtp, "[SmtpMailer] MAIL FROM refusé code={} from='{}' réponse='{}'", code, cfg.from_address, TrimSmtpResponse(resp));
			return false;
		}

		// RCPT TO
		if (!conn.Write("RCPT TO:<" + to + ">\r\n"))
		{
			LOG_ERROR(Smtp, "[SmtpMailer] Write RCPT TO failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 250)
		{
			LOG_ERROR(Smtp, "[SmtpMailer] RCPT TO refusé code={} réponse='{}'", code, TrimSmtpResponse(resp));
			return false;
		}

		// DATA
		if (!conn.Write("DATA\r\n"))
		{
			LOG_ERROR(Smtp, "[SmtpMailer] Write DATA failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 354)
		{
			LOG_ERROR(Smtp, "[SmtpMailer] DATA refusé code={} réponse='{}'", code, TrimSmtpResponse(resp));
			return false;
		}

		// Build and send message headers + body + end-of-data marker.
		std::string msg;
		msg.reserve(512u + body.size());
		msg += "From: " + cfg.from_address + "\r\n";
		msg += "To: " + to + "\r\n";
		msg += "Subject: " + subject + "\r\n";
		msg += "MIME-Version: 1.0\r\n";
		msg += isHtml ? "Content-Type: text/html; charset=UTF-8\r\n"
		              : "Content-Type: text/plain; charset=UTF-8\r\n";
		msg += "\r\n";
		msg += body;
		msg += "\r\n.\r\n";

		LOG_INFO(Smtp, "[SmtpMailer] envoi DATA (corps {} octets)", static_cast<int>(body.size()));

		if (!conn.Write(msg))
		{
			LOG_ERROR(Smtp, "[SmtpMailer] Write message body failed");
			return false;
		}
		code = conn.ReadResponse(resp);
		if (code != 250)
		{
			LOG_ERROR(Smtp, "[SmtpMailer] message refusé après DATA code={} réponse='{}'", code, TrimSmtpResponse(resp));
			return false;
		}

		// QUIT (best-effort; ignore response)
		conn.Write("QUIT\r\n");

		LOG_WARN(Smtp, "[SMTP] session terminée OK → {} (sujet {} car.)", RedactEmailForLog(to), static_cast<int>(subject.size()));
		LOG_INFO(Smtp, "[SmtpMailer] email envoyé OK to={} subject_len={}", RedactEmailForLog(to), static_cast<int>(subject.size()));
		return true;
	}
} // namespace engine::server

#else // !__unix__ — Windows stub

namespace engine::server
{
	bool SmtpMailer::Send(const SmtpConfig& /*cfg*/,
	                      const std::string& to,
	                      const std::string& subject,
	                      const std::string& /*body*/,
	                      bool /*isHtml*/)
	{
		LOG_WARN(Smtp, "[SmtpMailer] SMTP not implemented on Win32 — email not sent (to={} subject={})", RedactEmailForLog(to), subject);
		return false;
	}
} // namespace engine::server

#endif // __unix__
