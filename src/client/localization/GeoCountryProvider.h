#pragma once

#include <string>

namespace engine::client
{
	/// Abstraction du service de géolocalisation pays. Permet d'injecter un faux
	/// fournisseur dans les tests sans toucher au réseau.
	class IGeoCountryProvider
	{
	public:
		virtual ~IGeoCountryProvider() = default;

		/// Renvoie le code pays ISO-3166 alpha-2 (ex "FR") de l'IP publique appelante,
		/// ou "" en cas d'échec/timeout/hors-ligne. Appel **bloquant** (à exécuter sur
		/// un thread worker par l'appelant).
		virtual std::string FetchCountryCode() = 0;
	};
}
