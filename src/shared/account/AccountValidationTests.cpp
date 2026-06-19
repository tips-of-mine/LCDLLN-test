#include "src/shared/account/AccountValidation.h"

#include <iostream>
#include <string>

namespace
{
	int s_fail = 0;
	void Assert(bool cond, const char* msg)
	{
		if (!cond) { std::cerr << "[FAIL] " << msg << std::endl; ++s_fail; }
	}
	using engine::network::NetErrorCode;

	void TestPasswordRules()
	{
		auto a = engine::server::EvaluatePasswordRules("abc123z");
		Assert(!a.lengthOk, "len 7 -> lengthOk false");
		Assert(a.hasLetter && a.hasDigit, "abc123z -> lettre+chiffre");
		auto b = engine::server::EvaluatePasswordRules("abcd1234");
		Assert(b.lengthOk && b.hasLetter && b.hasDigit, "abcd1234 -> 3 règles OK");
		auto c = engine::server::EvaluatePasswordRules("12345678");
		Assert(c.lengthOk && c.hasDigit && !c.hasLetter, "12345678 -> sans lettre");
		auto d = engine::server::EvaluatePasswordRules("abcdefgh");
		Assert(d.lengthOk && d.hasLetter && !d.hasDigit, "abcdefgh -> sans chiffre");
	}

	void TestValidatePasswordEquivalence()
	{
		Assert(engine::server::ValidatePassword("abcd1234") == NetErrorCode::OK, "valid pw OK");
		Assert(engine::server::ValidatePassword("abc123z") == NetErrorCode::WEAK_PASSWORD, "len 7 weak");
		Assert(engine::server::ValidatePassword("12345678") == NetErrorCode::WEAK_PASSWORD, "no letter weak");
		Assert(engine::server::ValidatePassword("abcdefgh") == NetErrorCode::WEAK_PASSWORD, "no digit weak");
	}

	void TestValidateEmail()
	{
		using engine::server::ValidateEmail;
		using engine::server::NormaliseEmail;
		Assert(ValidateEmail(NormaliseEmail("a@b.com")) == NetErrorCode::OK, "a@b.com OK");
		Assert(ValidateEmail(NormaliseEmail("  A@B.COM ")) == NetErrorCode::OK, "trim+lower OK");
		Assert(ValidateEmail(NormaliseEmail("noat")) == NetErrorCode::INVALID_EMAIL, "no @");
		Assert(ValidateEmail(NormaliseEmail("@b.com")) == NetErrorCode::INVALID_EMAIL, "@ en tete");
		Assert(ValidateEmail(NormaliseEmail("a@bcom")) == NetErrorCode::INVALID_EMAIL, "domaine sans .");
		Assert(ValidateEmail(NormaliseEmail("")) == NetErrorCode::INVALID_EMAIL, "vide");
	}
}

int main()
{
	TestPasswordRules();
	TestValidatePasswordEquivalence();
	TestValidateEmail();
	if (s_fail != 0) return 1;
	std::cout << "AccountValidation tests: all passed." << std::endl;
	return 0;
}
