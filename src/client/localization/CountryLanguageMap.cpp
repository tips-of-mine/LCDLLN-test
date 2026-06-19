#include "src/client/localization/CountryLanguageMap.h"

#include "src/shared/core/Log.h"

#include <cctype>

namespace engine::client
{
	namespace
	{
		/// Parse un objet JSON plat {"clé":"valeur",...} sans dépendance externe.
		/// Réplique volontairement la tolérance du parseur de LocalizationService
		/// (mêmes fichiers de localisation, même format simple). Renvoie false
		/// si la structure n'est pas un objet plat de chaînes.
		bool ParseFlatStringObject(std::string_view text, std::unordered_map<std::string, std::string>& out)
		{
			out.clear();
			size_t pos = 0;
			auto skipWs = [&]() {
				while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0)
					++pos;
			};
			auto parseString = [&](std::string& s) -> bool {
				if (pos >= text.size() || text[pos] != '"')
					return false;
				++pos;
				s.clear();
				while (pos < text.size())
				{
					const char c = text[pos++];
					if (c == '"')
						return true;
					s.push_back(c);
				}
				return false;
			};

			skipWs();
			if (pos >= text.size() || text[pos] != '{')
				return false;
			++pos;
			for (;;)
			{
				skipWs();
				if (pos >= text.size())
					return false;
				if (text[pos] == '}')
					return true;
				std::string key;
				std::string value;
				if (!parseString(key))
					return false;
				skipWs();
				if (pos >= text.size() || text[pos] != ':')
					return false;
				++pos;
				skipWs();
				if (!parseString(value))
					return false;
				// Clé pays normalisée en MAJUSCULES pour un lookup insensible à la casse.
				for (char& ch : key)
					ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
				out[key] = value;
				skipWs();
				if (pos >= text.size())
					return false;                 // ni ',' ni '}' avant la fin
				if (text[pos] == ',')
				{
					++pos;
					continue;                     // la tête de boucle gère la clé suivante ou '}'
				}
				if (text[pos] == '}')
					return true;
				return false;                     // séparateur inattendu
			}
		}
	}

	bool CountryLanguageMap::LoadFromJson(std::string_view json)
	{
		if (!ParseFlatStringObject(json, m_map))
		{
			LOG_WARN(Core, "[CountryLanguageMap] JSON invalide, table vide");
			m_map.clear();
			return false;
		}
		LOG_INFO(Core, "[CountryLanguageMap] {} pays chargés", m_map.size());
		return true;
	}

	std::string CountryLanguageMap::LanguageForCountry(std::string_view countryCode) const
	{
		std::string key(countryCode);
		for (char& ch : key)
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		const auto it = m_map.find(key);
		if (it != m_map.end())
			return it->second;
		return "en";
	}
}
