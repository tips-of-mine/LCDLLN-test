#pragma once

#include <string>
#include <string_view>
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
}
