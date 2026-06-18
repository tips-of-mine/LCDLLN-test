#include "src/client/localization/LanguageSuggestionService.h"

#include <algorithm>
#include <utility>

namespace engine::client
{
	namespace
	{
		bool Contains(const std::vector<std::string>& v, std::string_view tag)
		{
			return std::find(v.begin(), v.end(), tag) != v.end();
		}
	}

	std::vector<std::string> ComputeSuggestedLocales(
		std::string_view systemLocale,
		std::string_view ipLocale,
		const std::vector<std::string>& availableCatalogs)
	{
		// Ordre voulu : système, IP, anglais.
		const std::string ordered[] = {
			std::string(systemLocale),
			std::string(ipLocale),
			std::string("en"),
		};

		std::vector<std::string> result;
		for (const std::string& tag : ordered)
		{
			if (tag.empty())
				continue;
			if (!Contains(availableCatalogs, tag))   // pas de catalogue -> jamais proposé
				continue;
			if (Contains(result, tag))               // déduplication
				continue;
			result.push_back(tag);
		}
		return result;
	}

	LanguageSuggestionService::~LanguageSuggestionService()
	{
		if (m_geoThread.joinable())
			m_geoThread.join();
	}

	void LanguageSuggestionService::BeginDetection(std::string_view systemLocale,
		const std::vector<std::string>& availableCatalogs,
		CountryLanguageMap countryMap,
		std::unique_ptr<IGeoCountryProvider> provider)
	{
		if (m_started)
			return;
		m_started = true;
		m_systemLocale = std::string(systemLocale);
		m_available = availableCatalogs;
		m_countryMap = std::move(countryMap);
		m_provider = std::move(provider);

		// Liste immédiate : {système, en}, sans attendre la géoloc.
		RecomputeSuggested();

		if (!m_provider)
		{
			m_geoFinished.store(true);
			return;
		}

		// Thread worker : appel bloquant du fournisseur, résultat stocké sous mutex.
		IGeoCountryProvider* provPtr = m_provider.get();
		m_geoThread = std::thread([this, provPtr]() {
			const std::string code = provPtr->FetchCountryCode();
			{
				std::lock_guard<std::mutex> lock(m_geoMutex);
				m_geoCountryCode = code;
			}
			m_geoFinished.store(true);
		});
	}

	bool LanguageSuggestionService::PollGeoUpdate()
	{
		if (!m_geoFinished.load())
			return false;
		if (m_geoConsumed.exchange(true))
			return false;   // déjà intégré

		if (m_geoThread.joinable())
			m_geoThread.join();

		std::string code;
		{
			std::lock_guard<std::mutex> lock(m_geoMutex);
			code = m_geoCountryCode;
		}
		if (code.empty())
			return false;   // échec géoloc : on garde {système, en}

		m_ipLocale = m_countryMap.LanguageForCountry(code);
		const std::vector<std::string> before = m_suggested;
		RecomputeSuggested();
		return m_suggested != before;
	}

	void LanguageSuggestionService::RecomputeSuggested()
	{
		m_suggested = ComputeSuggestedLocales(m_systemLocale, m_ipLocale, m_available);
	}
}
