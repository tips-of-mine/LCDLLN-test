#include "engine/server/SecurityAuditLog.h"
#include "engine/core/Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <chrono>
#include <cstring>
#include <format>
#include <string>

namespace engine::server
{
	SecurityAuditLog::~SecurityAuditLog()
	{
		Shutdown();
	}

	std::string SecurityAuditLog::timestamp()
	{
		auto now = std::chrono::system_clock::now();
		auto t = std::chrono::system_clock::to_time_t(now);
		char buf[32];
		std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
		return std::string(buf);
	}

	void SecurityAuditLog::writeLine(std::string_view event, std::string_view payload)
	{
		if (!m_logger)
			return;
		m_logger->info("{} [{}] {}", timestamp(), event, payload);
	}

	bool SecurityAuditLog::Init(std::string_view filePath, size_t rotationSizeMb, int retentionDays)
	{
		Shutdown();
		m_filePath = std::string(filePath);

		const size_t max_bytes = (rotationSizeMb > 0)
			? (rotationSizeMb * 1024u * 1024u)
			: (10u * 1024u * 1024u);
		const size_t max_files = (retentionDays > 0)
			? static_cast<size_t>(std::max(1, retentionDays))
			: 7u;

		try
		{
			auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
				m_filePath, max_bytes, max_files);
			m_logger = std::make_shared<spdlog::logger>("security_audit", sink);
			m_logger->set_level(spdlog::level::info);
			spdlog::register_logger(m_logger);
		}
		catch (const std::exception& e)
		{
			LOG_ERROR(Net, "[SecurityAuditLog] Init FAILED: cannot open file {} — {}", m_filePath, e.what());
			return false;
		}

		LOG_INFO(Net, "[SecurityAuditLog] Init OK: file={} (rotation_size_mb={}, retention_days={})",
			m_filePath, static_cast<unsigned>(rotationSizeMb), retentionDays);
		return true;
	}

	void SecurityAuditLog::Shutdown()
	{
		if (m_logger)
		{
			m_logger->flush();
			spdlog::drop("security_audit");
			m_logger.reset();
			LOG_INFO(Net, "[SecurityAuditLog] Shutdown: file closed");
		}
	}

	void SecurityAuditLog::LogLoginSuccess(std::string_view ip, uint64_t account_id, uint64_t session_id)
	{
		writeLine("LOGIN_SUCCESS", std::format("ip={} account_id={} session_id={}", ip, account_id, session_id));
	}

	void SecurityAuditLog::LogLoginFail(std::string_view ip, std::string_view reason)
	{
		writeLine("LOGIN_FAIL", std::format("ip={} reason={}", ip, reason));
	}

	void SecurityAuditLog::LogRegisterSuccess(std::string_view ip, uint64_t account_id)
	{
		writeLine("REGISTER_SUCCESS", std::format("ip={} account_id={}", ip, account_id));
	}

	void SecurityAuditLog::LogRegisterFail(std::string_view ip, std::string_view reason)
	{
		writeLine("REGISTER_FAIL", std::format("ip={} reason={}", ip, reason));
	}

	void SecurityAuditLog::LogBan(std::string_view ip, std::string_view reason)
	{
		writeLine("BAN", std::format("ip={} reason={}", ip, reason));
	}

	void SecurityAuditLog::LogUnban(std::string_view ip)
	{
		writeLine("UNBAN", std::format("ip={}", ip));
	}

	void SecurityAuditLog::LogSessionCreated(uint64_t session_id, uint64_t account_id)
	{
		writeLine("SESSION_CREATED", std::format("session_id={} account_id={}", session_id, account_id));
	}

	void SecurityAuditLog::LogSessionClosed(uint64_t session_id, std::string_view reason)
	{
		writeLine("SESSION_CLOSED", std::format("session_id={} reason={}", session_id, reason));
	}
}
