#include "src/client/localization/LanguageSuggestionService.h"

#include <algorithm>

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
}
