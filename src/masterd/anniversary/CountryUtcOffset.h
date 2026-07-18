#pragma once
// CountryUtcOffset — décalage UTC APPROXIMATIF par pays (ISO-3166-1 alpha-2,
// accounts.country_code) pour l'envoi de l'e-mail d'anniversaire « à 7 h du
// matin chez le joueur » (BirthdayEmailJob).
//
// APPROXIMATION assumée et documentée :
//  - fuseau REPRÉSENTATIF pour les pays multi-fuseaux (US → Central,
//    RU → Moscou, BR → Brasília, AU → Sydney, CA → Est…) ;
//  - heure STANDARD (pas d'heure d'été) : l'e-mail part entre 6 h et 8 h
//    locales dans le pire cas — largement suffisant pour un e-mail de vœux.
//  - pays inconnu / vide → UTC (0).
// Un vrai fuseau par compte (colonne dédiée, choix au portail) reste
// possible plus tard sans changer l'appelant.

#include <string_view>

namespace engine::server
{
	/// Décalage UTC en MINUTES pour \p countryCode ("FR" → +60). 0 si inconnu.
	int UtcOffsetMinutesForCountry(std::string_view countryCode);
}
