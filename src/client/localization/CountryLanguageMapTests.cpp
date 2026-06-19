#include "src/client/localization/CountryLanguageMap.h"

#include <iostream>

namespace
{
	int s_failCount = 0;

	void Assert(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	// Petite table JSON plate de test (sous-ensemble représentatif).
	constexpr const char* kTestJson =
		"{ \"FR\":\"fr\", \"ES\":\"es\", \"DE\":\"de\", \"BR\":\"pt\", \"GB\":\"en\" }";

	void TestMappingKnownCountries()
	{
		engine::client::CountryLanguageMap map;
		Assert(map.LoadFromJson(kTestJson), "load flat json");
		Assert(map.LanguageForCountry("FR") == "fr", "FR -> fr");
		Assert(map.LanguageForCountry("ES") == "es", "ES -> es");
		Assert(map.LanguageForCountry("DE") == "de", "DE -> de");
		Assert(map.LanguageForCountry("BR") == "pt", "BR -> pt");
	}

	void TestUnknownCountryDefaultsToEnglish()
	{
		engine::client::CountryLanguageMap map;
		Assert(map.LoadFromJson(kTestJson), "load flat json");
		Assert(map.LanguageForCountry("JP") == "en", "unknown JP -> en default");
		Assert(map.LanguageForCountry("") == "en", "empty -> en default");
	}

	void TestCaseInsensitiveCountryCode()
	{
		engine::client::CountryLanguageMap map;
		Assert(map.LoadFromJson(kTestJson), "load flat json");
		Assert(map.LanguageForCountry("fr") == "fr", "lowercase fr -> fr");
	}

	void TestMalformedJsonReturnsFalse()
	{
		engine::client::CountryLanguageMap map;
		Assert(!map.LoadFromJson(""), "chaîne vide -> false");
		Assert(!map.LoadFromJson("not json"), "JSON invalide -> false");
		Assert(map.Empty(), "table vide après parse invalide");
	}
}

int main()
{
	TestMappingKnownCountries();
	TestUnknownCountryDefaultsToEnglish();
	TestCaseInsensitiveCountryCode();
	TestMalformedJsonReturnsFalse();
	if (s_failCount != 0)
		return 1;
	std::cout << "CountryLanguageMap tests: all passed." << std::endl;
	return 0;
}
