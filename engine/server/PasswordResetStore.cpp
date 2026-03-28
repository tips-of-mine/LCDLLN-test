// M33.2 — PasswordResetStore implementation.

#include "engine/server/PasswordResetStore.h"
#include "engine/auth/Argon2Hash.h"
#include "engine/core/Log.h"

#include <cstdio>
#include <cstdint>

namespace engine::server
{
	namespace
	{
		/// Generates a 32-char hex token from 16 random bytes (via engine::auth::GenerateSalt).
		static std::string GenerateHexToken()
		{
			auto bytes = engine::auth::GenerateSalt(16);
			if (bytes.empty())
				return {};
			std::string token;
			token.reserve(32);
			for (uint8_t b : bytes)
			{
				char buf[3];
				std::snprintf(buf, sizeof(buf), "%02x", static_cast<unsigned>(b));
				token += buf;
			}
			return token;
		}

		/// Generates a 6-digit decimal verification code from 4 random bytes.
		static std::string GenerateSixDigitCode()
		{
			auto bytes = engine::auth::GenerateSalt(4);
			if (bytes.empty())
				return {};
			uint32_t val = 0;
			for (int i = 0; i < 4; ++i)
				val = (val << 8u) | static_cast<uint32_t>(bytes[static_cast<size_t>(i)]);
			val %= 1000000u;
			char buf[7];
			std::snprintf(buf, sizeof(buf), "%06u", val);
			return std::string(buf);
		}
	} // anonymous namespace

	std::string PasswordResetStore::CreateResetToken(uint64_t account_id)
	{
		std::string token = GenerateHexToken();
		if (token.empty())
		{
			LOG_ERROR(Auth, "[PasswordResetStore] CreateResetToken: GenerateSalt failed (account_id={})", account_id);
			return {};
		}
		using namespace std::chrono;
		ResetTokenEntry entry;
		entry.account_id = account_id;
		entry.expires_at = Clock::now() + hours(1);
		entry.used       = false;
		m_reset_tokens[token] = std::move(entry);
		LOG_INFO(Auth, "[PasswordResetStore] Reset token created (account_id={} token_prefix={}...)", account_id, token.substr(0, 8));
		return token;
	}

	std::optional<uint64_t> PasswordResetStore::ValidateResetToken(const std::string& token) const
	{
		auto it = m_reset_tokens.find(token);
		if (it == m_reset_tokens.end())
		{
			LOG_WARN(Auth, "[PasswordResetStore] ValidateResetToken: token not found");
			return std::nullopt;
		}
		if (it->second.used)
		{
			LOG_WARN(Auth, "[PasswordResetStore] ValidateResetToken: token already used (account_id={})", it->second.account_id);
			return std::nullopt;
		}
		if (Clock::now() > it->second.expires_at)
		{
			LOG_WARN(Auth, "[PasswordResetStore] ValidateResetToken: token expired (account_id={})", it->second.account_id);
			return std::nullopt;
		}
		return it->second.account_id;
	}

	bool PasswordResetStore::MarkResetTokenUsed(const std::string& token)
	{
		auto it = m_reset_tokens.find(token);
		if (it == m_reset_tokens.end())
		{
			LOG_WARN(Auth, "[PasswordResetStore] MarkResetTokenUsed: token not found");
			return false;
		}
		it->second.used = true;
		LOG_INFO(Auth, "[PasswordResetStore] Reset token marked used (account_id={})", it->second.account_id);
		return true;
	}

	std::string PasswordResetStore::CreateVerificationCode(uint64_t account_id)
	{
		std::string code = GenerateSixDigitCode();
		if (code.empty())
		{
			LOG_ERROR(Auth, "[PasswordResetStore] CreateVerificationCode: GenerateSalt failed (account_id={})", account_id);
			return {};
		}
		using namespace std::chrono;
		VerificationEntry entry;
		entry.code       = code;
		entry.expires_at = Clock::now() + hours(24);
		entry.verified   = false;
		m_verifications[account_id] = std::move(entry);
		LOG_INFO(Auth, "[PasswordResetStore] Verification code created (account_id={})", account_id);
		return code;
	}

	bool PasswordResetStore::ValidateVerificationCode(uint64_t account_id, const std::string& code) const
	{
		auto it = m_verifications.find(account_id);
		if (it == m_verifications.end())
		{
			LOG_WARN(Auth, "[PasswordResetStore] ValidateVerificationCode: no entry for account_id={}", account_id);
			return false;
		}
		if (it->second.verified)
		{
			LOG_WARN(Auth, "[PasswordResetStore] ValidateVerificationCode: already verified (account_id={})", account_id);
			return false;
		}
		if (Clock::now() > it->second.expires_at)
		{
			LOG_WARN(Auth, "[PasswordResetStore] ValidateVerificationCode: code expired (account_id={})", account_id);
			return false;
		}
		if (it->second.code != code)
		{
			LOG_WARN(Auth, "[PasswordResetStore] ValidateVerificationCode: code mismatch (account_id={})", account_id);
			return false;
		}
		return true;
	}

	bool PasswordResetStore::MarkEmailVerified(uint64_t account_id)
	{
		auto it = m_verifications.find(account_id);
		if (it == m_verifications.end())
		{
			LOG_WARN(Auth, "[PasswordResetStore] MarkEmailVerified: no entry for account_id={}", account_id);
			return false;
		}
		it->second.verified = true;
		LOG_INFO(Auth, "[PasswordResetStore] Email verification confirmed (account_id={})", account_id);
		return true;
	}

	bool PasswordResetStore::CanSendEmail(uint64_t account_id) const
	{
		auto it = m_email_rate.find(account_id);
		if (it == m_email_rate.end())
			return true;
		using namespace std::chrono;
		const auto& entry = it->second;
		if (Clock::now() - entry.window_start >= hours(1))
			return true; // Window has expired; will be reset on RecordEmailSent.
		return entry.count < kMaxEmailsPerHour;
	}

	void PasswordResetStore::RecordEmailSent(uint64_t account_id)
	{
		using namespace std::chrono;
		auto& entry = m_email_rate[account_id];
		if (Clock::now() - entry.window_start >= hours(1))
		{
			entry.count        = 1;
			entry.window_start = Clock::now();
		}
		else
		{
			entry.count++;
		}
		LOG_DEBUG(Auth, "[PasswordResetStore] RecordEmailSent (account_id={} count_in_window={})", account_id, entry.count);
	}

} // namespace engine::server
