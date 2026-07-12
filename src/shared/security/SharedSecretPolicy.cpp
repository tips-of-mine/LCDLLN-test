#include "src/shared/security/SharedSecretPolicy.h"

#include <array>
#include <cstdlib>
#include <string_view>
#if defined(_MSC_VER)
#pragma warning(disable : 4996) // getenv (lecture var env, sur) ; deprecation CRT MSVC
#endif

namespace engine::security
{
	bool IsWeakSharedSecret(std::string_view secret)
	{
		if (secret.empty())
			return true;
		static constexpr std::array<std::string_view, 2> kKnownDevSecrets = {
			"dev_secret_change_in_production",
			"changeme",
		};
		for (std::string_view weak : kKnownDevSecrets)
			if (secret == weak)
				return true;
		return false;
	}

	bool DevSecretOverrideEnabled()
	{
		const char* v = std::getenv("LCDLLN_ALLOW_DEV_SECRET");
		return v != nullptr && std::string_view(v) == "1";
	}
}
