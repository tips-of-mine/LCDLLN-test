#include "src/client/render/gi/DdgiQuality.h"

#include <cctype>
#include <string>

namespace engine::render::gi
{
	namespace
	{
		/// Met une chaîne en minuscules (ASCII) pour la comparaison insensible
		/// à la casse. CPU pur, sans dépendance locale.
		std::string ToLowerAscii(std::string_view s)
		{
			std::string out;
			out.reserve(s.size());
			for (char c : s)
				out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
			return out;
		}
	}

	DdgiQuality ParseDdgiQuality(std::string_view s)
	{
		const std::string v = ToLowerAscii(s);
		if (v == "off")
			return DdgiQuality::Off;
		if (v == "static" || v == "static-probes")
			return DdgiQuality::StaticProbes;
		if (v == "dynamic-low")
			return DdgiQuality::DynamicLow;
		if (v == "dynamic-high")
			return DdgiQuality::DynamicHigh;
		// Inconnu / vide : défaut Off (une coquille ne doit jamais activer la DDGI).
		return DdgiQuality::Off;
	}

	DdgiQualitySettings ResolveDdgiQuality(DdgiQuality q)
	{
		switch (q)
		{
			// Off et StaticProbes => dynamic=false => useDdgi=0 => rendu probes
			// statiques inchangé (garantie de non-régression).
			case DdgiQuality::Off:
				return DdgiQualitySettings{ /*dynamic*/ false, /*updateDivisor*/ 4u, /*intensity*/ 0.0f };
			case DdgiQuality::StaticProbes:
				return DdgiQualitySettings{ /*dynamic*/ false, /*updateDivisor*/ 4u, /*intensity*/ 0.0f };
			case DdgiQuality::DynamicLow:
				return DdgiQualitySettings{ /*dynamic*/ true, /*updateDivisor*/ 8u, /*intensity*/ 0.5f };
			case DdgiQuality::DynamicHigh:
				return DdgiQualitySettings{ /*dynamic*/ true, /*updateDivisor*/ 2u, /*intensity*/ 1.0f };
		}
		// Défensif : tout enum non couvert retombe sur Off (statique).
		return DdgiQualitySettings{ false, 4u, 0.0f };
	}

	const char* DdgiQualityName(DdgiQuality q)
	{
		switch (q)
		{
			case DdgiQuality::Off:          return "off";
			case DdgiQuality::StaticProbes: return "static-probes";
			case DdgiQuality::DynamicLow:   return "dynamic-low";
			case DdgiQuality::DynamicHigh:  return "dynamic-high";
		}
		return "off";
	}
} // namespace engine::render::gi
