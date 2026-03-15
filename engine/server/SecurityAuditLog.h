#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace engine::server
{
	/// Dedicated security audit logger. Writes to a separate file. Never logs passwords or hashes.
	class SecurityAuditLog
	{
	public:
		SecurityAuditLog() = default;
		~SecurityAuditLog();

		/// Open audit log file at \a filePath. Returns true on success. Existing file is appended.
		bool Init(std::string_view filePath);

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

		void* m_file = nullptr;
		std::string m_filePath;
	};

}
