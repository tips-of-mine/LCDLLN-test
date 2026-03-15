#include "engine/server/AccountValidation.h"

#include <algorithm>
#include <cctype>

namespace engine::server
{
	namespace
	{
		std::string_view TrimView(std::string_view s)
		{
			auto start = s.find_first_not_of(" \t\r\n");
			if (start == std::string_view::npos)
				return {};
			auto end = s.find_last_not_of(" \t\r\n");
			return s.substr(start, end == std::string_view::npos ? s.size() - start : end - start + 1);
		}
	}

	std::string NormaliseEmail(std::string_view input)
	{
		std::string_view trimmed = TrimView(input);
		std::string out;
		out.reserve(trimmed.size());
		for (unsigned char c : trimmed)
			out += static_cast<char>(std::tolower(c));
		return out;
	}

	std::string_view NormaliseLoginView(std::string_view input)
	{
		return TrimView(input);
	}

	engine::network::NetErrorCode ValidateEmail(std::string_view normalisedEmail)
	{
		if (normalisedEmail.empty())
			return engine::network::NetErrorCode::INVALID_EMAIL;
		if (normalisedEmail.size() > kAccountEmailMaxLength)
			return engine::network::NetErrorCode::INVALID_EMAIL;
		std::size_t at = normalisedEmail.find('@');
		if (at == std::string_view::npos || at == 0)
			return engine::network::NetErrorCode::INVALID_EMAIL;
		std::string_view domain = normalisedEmail.substr(at + 1);
		if (domain.empty())
			return engine::network::NetErrorCode::INVALID_EMAIL;
		if (domain.find('.') == std::string_view::npos)
			return engine::network::NetErrorCode::INVALID_EMAIL;
		return engine::network::NetErrorCode::OK;
	}

	engine::network::NetErrorCode ValidateLogin(std::string_view normalisedLogin)
	{
		if (normalisedLogin.size() < kAccountLoginMinLength || normalisedLogin.size() > kAccountLoginMaxLength)
			return engine::network::NetErrorCode::INVALID_LOGIN;
		for (unsigned char c : normalisedLogin)
		{
			bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
			if (!ok)
				return engine::network::NetErrorCode::INVALID_LOGIN;
		}
		return engine::network::NetErrorCode::OK;
	}

	engine::network::NetErrorCode ValidatePassword(std::string_view password)
	{
		if (password.size() < kAccountPasswordMinLength || password.size() > kAccountPasswordMaxLength)
			return engine::network::NetErrorCode::WEAK_PASSWORD;
		bool hasDigit = false;
		bool hasLetter = false;
		for (unsigned char c : password)
		{
			if (std::isdigit(c))
				hasDigit = true;
			if (std::isalpha(static_cast<unsigned char>(c)))
				hasLetter = true;
		}
		if (!hasDigit || !hasLetter)
			return engine::network::NetErrorCode::WEAK_PASSWORD;
		return engine::network::NetErrorCode::OK;
	}
}
