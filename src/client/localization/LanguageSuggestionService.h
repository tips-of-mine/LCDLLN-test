#pragma once

#include "src/client/localization/CountryLanguageMap.h"
#include "src/client/localization/GeoCountryProvider.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace engine::client
{
	/// Calcule la liste ordonnée et filtrée des langues à proposer au 1er lancement.
	///
	/// union = dédupliquer([ systemLocale, ipLocale, "en" ]) ∩ availableCatalogs
	///
	/// Ordre : langue système d'abord (carte sélectionnée par défaut), puis langue
	/// IP si différente, puis anglais s'il n'est pas déjà présent. "en" est toujours
	/// ajouté avant le filtrage ; le filtre ∩ availableCatalogs garantit qu'on ne
	/// propose jamais une langue sans catalogue. Résultat vide impossible tant que
	/// "en" est dans availableCatalogs.
	///
	/// \param systemLocale tag langue système normalisé (ex "fr"), peut être vide.
	/// \param ipLocale     tag langue déduit du pays IP, "" si géoloc indisponible.
	/// \param availableCatalogs locales ayant réellement un catalogue (ordre indifférent).
	std::vector<std::string> ComputeSuggestedLocales(
		std::string_view systemLocale,
		std::string_view ipLocale,
		const std::vector<std::string>& availableCatalogs);

	/// Orchestre la suggestion de langues au 1er lancement : calcule immédiatement
	/// {système, en}, lance la géoloc sur un thread worker, et intègre la langue du
	/// pays IP quand la réponse arrive. Tout est best-effort : un échec laisse la
	/// liste {système, en}.
	class LanguageSuggestionService final
	{
	public:
		LanguageSuggestionService() = default;
		~LanguageSuggestionService();

		LanguageSuggestionService(const LanguageSuggestionService&) = delete;
		LanguageSuggestionService& operator=(const LanguageSuggestionService&) = delete;

		/// Démarre la détection. Calcule la liste initiale {système, en} et lance le
		/// fournisseur géo sur un thread. Idempotent : un second appel est ignoré.
		/// \param systemLocale tag langue système (ex "fr").
		/// \param availableCatalogs locales ayant un catalogue.
		/// \param countryMap table pays->langue (copiée).
		/// \param provider fournisseur géo (propriété transférée) ; nullptr -> pas de géoloc.
		void BeginDetection(std::string_view systemLocale,
			const std::vector<std::string>& availableCatalogs,
			CountryLanguageMap countryMap,
			std::unique_ptr<IGeoCountryProvider> provider);

		/// À appeler chaque frame (main thread). Si le résultat géo vient d'arriver,
		/// recalcule la liste suggérée et renvoie true (le presenter doit reconstruire
		/// son modèle). Renvoie false sinon.
		bool PollGeoUpdate();

		/// true une fois que le thread géo a terminé (succès ou échec).
		bool GeoDetectionFinished() const { return m_geoFinished.load(); }

		/// Liste suggérée courante (thread-safe en lecture main thread après Poll).
		std::vector<std::string> GetSuggestedLocales() const { return m_suggested; }

	private:
		void RecomputeSuggested();   // main thread uniquement

		std::string m_systemLocale;
		std::string m_ipLocale;                  // rempli après géoloc réussie
		std::vector<std::string> m_available;
		std::vector<std::string> m_suggested;
		CountryLanguageMap m_countryMap;

		std::unique_ptr<IGeoCountryProvider> m_provider;
		std::thread m_geoThread;
		std::atomic<bool> m_geoFinished{ false };
		std::atomic<bool> m_geoConsumed{ false };
		std::mutex m_geoMutex;
		std::string m_geoCountryCode;            // protégé par m_geoMutex
		bool m_started = false;
	};
}
