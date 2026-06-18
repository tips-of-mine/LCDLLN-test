#include "src/client/localization/LanguageSuggestionService.h"

#include <iostream>
#include <vector>

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

	using Vec = std::vector<std::string>;

	// Univers complet des catalogues (cas nominal : les 7 langues existent).
	const Vec kAll7 = { "de", "en", "es", "fr", "it", "pl", "pt" };

	void TestExampleFranceFrench()
	{
		// Poste FR, système FR, IP=fr -> {fr, en}, système d'abord.
		const Vec got = engine::client::ComputeSuggestedLocales("fr", "fr", kAll7);
		Assert(got == Vec({ "fr", "en" }), "FR+FR -> {fr, en}");
	}

	void TestExampleSpainGerman()
	{
		// Poste ES, système DE, IP=es -> {de, es, en} : système, puis IP, puis en.
		const Vec got = engine::client::ComputeSuggestedLocales("de", "es", kAll7);
		Assert(got == Vec({ "de", "es", "en" }), "DE+ES -> {de, es, en}");
	}

	void TestGeoFailureFallsBackToSystemPlusEnglish()
	{
		// IP inconnue ("") -> {système, en}.
		const Vec got = engine::client::ComputeSuggestedLocales("de", "", kAll7);
		Assert(got == Vec({ "de", "en" }), "DE+(none) -> {de, en}");
	}

	void TestSystemEqualsIpDeduplicates()
	{
		const Vec got = engine::client::ComputeSuggestedLocales("es", "es", kAll7);
		Assert(got == Vec({ "es", "en" }), "ES+ES dedup -> {es, en}");
	}

	void TestLocaleWithoutCatalogExcluded()
	{
		// Seuls fr/en ont un catalogue (état actuel du repo). IP=es mais pas de
		// catalogue es -> exclu, on retombe sur {fr, en}.
		const Vec only2 = { "en", "fr" };
		const Vec got = engine::client::ComputeSuggestedLocales("fr", "es", only2);
		Assert(got == Vec({ "fr", "en" }), "es sans catalogue exclu -> {fr, en}");
	}

	void TestEnglishAlwaysPresentEvenIfSystemUnavailable()
	{
		// Système = pl mais catalogues = {en} -> {en} seulement (en toujours là).
		const Vec onlyEn = { "en" };
		const Vec got = engine::client::ComputeSuggestedLocales("pl", "fr", onlyEn);
		Assert(got == Vec({ "en" }), "tout indispo sauf en -> {en}");
	}
}

int main()
{
	TestExampleFranceFrench();
	TestExampleSpainGerman();
	TestGeoFailureFallsBackToSystemPlusEnglish();
	TestSystemEqualsIpDeduplicates();
	TestLocaleWithoutCatalogExcluded();
	TestEnglishAlwaysPresentEvenIfSystemUnavailable();
	if (s_failCount != 0)
		return 1;
	std::cout << "LanguageSuggestionService tests: all passed." << std::endl;
	return 0;
}
