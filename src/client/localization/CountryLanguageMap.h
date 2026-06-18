#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::client
{
	/// Table pays (ISO-3166 alpha-2) -> tag de langue court (fr/en/es/de/it/pl/pt).
	/// Chargée depuis game/data/localization/country_language.json. Tout pays absent
	/// retombe sur "en" (filet de sécurité universel).
	class CountryLanguageMap final
	{
	public:
		/// Charge la table depuis un texte JSON plat {"FR":"fr",...}. Renvoie false si parse invalide.
		bool LoadFromJson(std::string_view json);

		/// Langue associée au code pays (insensible à la casse). "en" par défaut si inconnu/vide.
		std::string LanguageForCountry(std::string_view countryCode) const;

		/// true si au moins une entrée a été chargée.
		bool Empty() const { return m_map.empty(); }

	private:
		std::unordered_map<std::string, std::string> m_map;
	};
}
