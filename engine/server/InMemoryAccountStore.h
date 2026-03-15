#pragma once

#include "engine/network/NetErrorCode.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server
{
	/// Account status for auth gating (M20.5).
	enum class AccountStatus : uint8_t
	{
		Active,
		Locked
	};

	/// In-memory account record (v1, no DB). Stores final_hash (Argon2 encoded) per M20.2; no characters.
	struct AccountRecord
	{
		uint64_t account_id = 0;
		std::string login;
		std::string email;
		std::string final_hash;  // Argon2 encoded (contains salt)
		AccountStatus status = AccountStatus::Active;
	};

	/// In-memory account store for Master auth/register (M20.5). No persistence; no characters.
	/// Thread-safe only if caller serialises access (v1 single-threaded worker).
	class InMemoryAccountStore
	{
	public:
		InMemoryAccountStore() = default;

		/// Create account: validates login and email (if non-empty), checks login/email not taken,
		/// computes final_hash = Argon2(client_hash, server_salt) and stores. Returns account_id or 0 on failure.
		/// \param client_hash Argon2-encoded client hash (from client); never stored as-is, only hashed again.
		uint64_t CreateAccount(std::string_view login, std::string_view email, std::string_view client_hash);

		/// Lookup by normalised login. Returns nullopt if not found.
		std::optional<AccountRecord> FindByLogin(std::string_view normalisedLogin) const;

		/// Lookup by account_id. Returns nullopt if not found.
		std::optional<AccountRecord> FindByAccountId(uint64_t account_id) const;

		/// Returns true if an account with this normalised email already exists.
		bool ExistsEmail(std::string_view normalisedEmail) const;

		/// Returns true if an account with this normalised login already exists.
		bool ExistsLogin(std::string_view normalisedLogin) const;

	private:
		uint64_t m_nextAccountId = 1;
		std::unordered_map<std::string, AccountRecord> m_by_login;
		std::unordered_map<std::string, uint64_t> m_by_email;  // normalised email -> account_id
	};

}
