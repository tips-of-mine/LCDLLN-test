#include "engine/server/SecurityAuditLog.h"
#include "engine/core/Log.h"
#include <cstdio>
#include <ctime>
#include <chrono>
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
		if (!m_file)
			return;
		std::string line = std::format("{} [{}] {}\n", timestamp(), event, payload);
		std::fputs(line.c_str(), static_cast<std::FILE*>(m_file));
		std::fflush(static_cast<std::FILE*>(m_file));
	}

	bool SecurityAuditLog::Init(std::string_view filePath)
	{
		Shutdown();
		m_filePath = std::string(filePath);
		std::FILE* f = std::fopen(m_filePath.c_str(), "a");
		if (!f)
		{
			LOG_ERROR(Net, "[SecurityAuditLog] Init FAILED: cannot open file {}", m_filePath);
			return false;
		}
		m_file = f;
		LOG_INFO(Net, "[SecurityAuditLog] Init OK: file={}", m_filePath);
		return true;
	}

	void SecurityAuditLog::Shutdown()
	{
		if (m_file)
		{
			std::fclose(static_cast<std::FILE*>(m_file));
			m_file = nullptr;
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
