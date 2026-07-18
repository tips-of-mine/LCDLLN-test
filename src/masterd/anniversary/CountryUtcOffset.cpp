// CountryUtcOffset — implémentation. Voir le header pour l'approximation
// (fuseau représentatif, heure standard, défaut UTC).

#include "src/masterd/anniversary/CountryUtcOffset.h"

namespace engine::server
{
	namespace
	{
		struct CountryOffset
		{
			const char* code;
			int offsetMinutes;
		};

		/// Heure STANDARD, fuseau représentatif. Trié par zones pour relecture.
		constexpr CountryOffset kOffsets[] = {
			// Europe de l'Ouest / centrale
			{ "GB", 0 },    { "IE", 0 },    { "PT", 0 },
			{ "FR", 60 },   { "BE", 60 },   { "NL", 60 },  { "LU", 60 },
			{ "DE", 60 },   { "CH", 60 },   { "AT", 60 },  { "ES", 60 },
			{ "IT", 60 },   { "PL", 60 },   { "CZ", 60 },  { "DK", 60 },
			{ "SE", 60 },   { "NO", 60 },   { "HU", 60 },
			{ "FI", 120 },  { "GR", 120 },  { "RO", 120 }, { "BG", 120 },
			{ "UA", 120 },  { "TR", 180 },  { "RU", 180 },
			// Afrique / Moyen-Orient
			{ "MA", 60 },   { "DZ", 60 },   { "TN", 60 },
			{ "SN", 0 },    { "CI", 0 },    { "CM", 60 },  { "CD", 60 },
			{ "EG", 120 },  { "ZA", 120 },  { "IL", 120 },
			{ "SA", 180 },  { "AE", 240 },
			{ "MG", 180 },  { "RE", 240 },  { "MU", 240 },
			// Amériques
			{ "CA", -300 }, { "US", -360 }, { "MX", -360 },
			{ "CO", -300 }, { "PE", -300 }, { "VE", -240 }, { "CL", -240 },
			{ "BR", -180 }, { "AR", -180 }, { "UY", -180 },
			{ "GP", -240 }, { "MQ", -240 }, { "GF", -180 },
			// Asie / Océanie
			{ "IN", 330 },  { "TH", 420 },  { "VN", 420 }, { "ID", 420 },
			{ "CN", 480 },  { "TW", 480 },  { "HK", 480 }, { "SG", 480 },
			{ "MY", 480 },  { "PH", 480 },
			{ "JP", 540 },  { "KR", 540 },
			{ "AU", 600 },  { "NC", 660 },  { "NZ", 720 }, { "PF", -600 },
		};
	}

	int UtcOffsetMinutesForCountry(std::string_view countryCode)
	{
		if (countryCode.size() != 2) return 0;
		for (const CountryOffset& c : kOffsets)
		{
			if (countryCode[0] == c.code[0] && countryCode[1] == c.code[1])
				return c.offsetMinutes;
		}
		return 0;
	}
}
