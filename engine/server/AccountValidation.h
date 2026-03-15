#pragma once

#include "engine/network/NetErrorCode.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace engine::server
{
	/// Account schema v1 constants (see tickets/docs/accounts_schema_v1.md).
	constexpr size_t kAccountEmailMaxLength = 256u;
	constexpr size_t kAccountLoginMinLength = 3u;
	constexpr size_t kAccountLoginMaxLength = 64u;
	constexpr size_t kAccountPasswordMinLength = 8u;
	constexpr size_t kAccountPasswordMaxLength = 256u;

	/// Normalises email for validation: trim, then lower-case. Result may be empty.
	/// \param input Raw input (e.g. from client).
	/// \return Normalised string (trimmed, lower-case); empty if input empty or only whitespace.
	std::string NormaliseEmail(std::string_view input);

	/// Normalises login for validation: trim only.
	/// \param input Raw input.
	/// \return Trimmed string view (no copy); valid only while \a input is valid.
	std::string_view NormaliseLoginView(std::string_view input);

	/// Validates email (format + length). Uses normalised form (trim + lower-case).
	/// \param normalisedEmail Email after NormaliseEmail().
	/// \return OK if valid, or INVALID_EMAIL on format/length error.
	engine::network::NetErrorCode ValidateEmail(std::string_view normalisedEmail);

	/// Validates login (charset: alphanumeric + underscore, length 3–64).
	/// \param normalisedLogin Login after trim (e.g. NormaliseLoginView).
	/// \return OK if valid, or INVALID_LOGIN on charset/length error.
	engine::network::NetErrorCode ValidateLogin(std::string_view normalisedLogin);

	/// Validates password (policy v1: min 8, max 256, at least one digit and one letter).
	/// Plaintext only for validation; never store. Does not normalise.
	/// \param password Plaintext password (will not be stored).
	/// \return OK if valid, or WEAK_PASSWORD on length/complexity error.
	engine::network::NetErrorCode ValidatePassword(std::string_view password);
}
