#pragma once

#include "src/client/localization/GeoCountryProvider.h"

#include <cstdint>

namespace engine::client
{
	/// Fournisseur géo basé sur ip-api.com : GET http://ip-api.com/json/ (sans IP :
	/// géolocalise l'appelant) puis extrait le champ "countryCode". Implémenté via
	/// WinHTTP sous Windows ; no-op (renvoie "") sur les autres plateformes.
	///
	/// ⚠️ ip-api.com gratuit est HTTP non chiffré ; best-effort, jamais bloquant pour
	/// l'UI (l'appelant l'exécute sur un thread worker).
	class IpApiGeoProvider final : public IGeoCountryProvider
	{
	public:
		/// \param timeoutMs délai max total de la requête (défaut 2000 ms).
		explicit IpApiGeoProvider(uint32_t timeoutMs = 2000u) : m_timeoutMs(timeoutMs) {}

		std::string FetchCountryCode() override;

	private:
		uint32_t m_timeoutMs;
	};
}
