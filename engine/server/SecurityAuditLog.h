#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace spdlog
{
	class logger;
}

namespace engine::server
{
	/// Dedicated security audit logger. Writes to a separate file. Never logs passwords or hashes.
	class SecurityAuditLog
	{
	public:
		SecurityAuditLog() = default;
		~SecurityAuditLog();

		/// Open audit log file at \a filePath with rotation. Returns true on success.
		/// \a rotationSizeMb max size per file in MB before rotation; \a retentionDays number of files to retain.
		bool Init(std::string_view filePath, size_t rotationSizeMb = 10, int retentionDays = 7);

		/// Close file. Safe to call multiple times.
		void Shutdown();

		/// Log login success. Do not pass password/hash.
		void LogLoginSuccess(std::string_view ip, uint64_t account_id, uint64_t session_id);

		/// Log login failure. Do not pass password/hash.
		void LogLoginFail(std::string_view ip, std::string_view reason);

		/// Log register success. Do not pass password/hash.
		void LogRegisterSuccess(std::string_view ip, uint64_t account_id);

		/// Log register failure. Do not pass password/hash.
		void LogRegisterFail(std::string_view ip, std::string_view reason);

		/// Log IP ban (e.g. after too many failures).
		void LogBan(std::string_view ip, std::string_view reason);

		/// Log IP unban (expiry or admin).
		void LogUnban(std::string_view ip);

		/// Log session created (correlate with M20.3).
		void LogSessionCreated(uint64_t session_id, uint64_t account_id);

		/// Log session closed.
		void LogSessionClosed(uint64_t session_id, std::string_view reason);

	private:
		void writeLine(std::string_view event, std::string_view payload);
		static std::string timestamp();

		std::shared_ptr<spdlog::logger> m_logger;
		std::string m_filePath;
	};

}
