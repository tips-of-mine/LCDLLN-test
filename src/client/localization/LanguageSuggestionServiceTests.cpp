#include "src/client/localization/LanguageSuggestionService.h"
#include "src/client/localization/CountryLanguageMap.h"
#include "src/client/localization/GeoCountryProvider.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
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

	// Faux fournisseur : renvoie un code pays figé (ou "" pour simuler un échec),
	// sans aucun accès réseau.
	class FakeGeoProvider final : public engine::client::IGeoCountryProvider
	{
	public:
		explicit FakeGeoProvider(std::string code) : m_code(std::move(code)) {}
		std::string FetchCountryCode() override { return m_code; }
	private:
		std::string m_code;
	};

	engine::client::CountryLanguageMap MakeMap()
	{
		engine::client::CountryLanguageMap m;
		m.LoadFromJson("{ \"FR\":\"fr\", \"ES\":\"es\", \"DE\":\"de\" }");
		return m;
	}

	// Attend (de façon déterministe, borne dure) que le thread géo se termine,
	// puis consomme le résultat via un dernier Poll. sleep_for évite l'attente
	// active flaky (le worker doit être ordonnancé avant qu'on vérifie).
	void PumpUntilDone(engine::client::LanguageSuggestionService& svc)
	{
		for (int i = 0; i < 2000 && !svc.GeoDetectionFinished(); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		svc.PollGeoUpdate();   // intègre le résultat (recompute) si arrivé
	}

	void TestAsyncSpainGermanProducesThree()
	{
		// Catalogues complets, système DE, IP -> ES.
		const Vec all = { "de", "en", "es", "fr" };
		engine::client::LanguageSuggestionService svc;
		svc.BeginDetection("de", all, MakeMap(),
			std::make_unique<FakeGeoProvider>("ES"));
		PumpUntilDone(svc);
		svc.PollGeoUpdate();
		Assert(svc.GetSuggestedLocales() == Vec({ "de", "es", "en" }), "async DE+ES -> {de, es, en}");
	}

	void TestAsyncFailureKeepsSystemAndEnglish()
	{
		const Vec all = { "de", "en", "es", "fr" };
		engine::client::LanguageSuggestionService svc;
		svc.BeginDetection("de", all, MakeMap(),
			std::make_unique<FakeGeoProvider>(""));   // échec géoloc
		PumpUntilDone(svc);
		svc.PollGeoUpdate();
		Assert(svc.GetSuggestedLocales() == Vec({ "de", "en" }), "async échec -> {de, en}");
	}

	void TestImmediateListBeforeGeo()
	{
		// Avant toute réponse géo, la liste vaut déjà {système, en}.
		const Vec all = { "de", "en", "es", "fr" };
		engine::client::LanguageSuggestionService svc;
		svc.BeginDetection("de", all, MakeMap(),
			std::make_unique<FakeGeoProvider>("ES"));
		// Sans pomper : la liste initiale ne contient pas encore la langue IP.
		const Vec initial = svc.GetSuggestedLocales();
		Assert(initial.front() == "de", "liste initiale commence par le système");
		Assert(std::find(initial.begin(), initial.end(), "en") != initial.end(), "en présent d'emblée");
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
	TestAsyncSpainGermanProducesThree();
	TestAsyncFailureKeepsSystemAndEnglish();
	TestImmediateListBeforeGeo();
	if (s_failCount != 0)
		return 1;
	std::cout << "LanguageSuggestionService tests: all passed." << std::endl;
	return 0;
}
