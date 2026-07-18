#pragma once
// AnniversaryMath — logique calendaire PURE des récompenses d'anniversaire
// (spec docs/superpowers/specs/2026-07-18-anniversary-rewards-design.md).
// Aucune dépendance DB/réseau : consommé par le master (AnniversaryService,
// validation d'inscription) et testé par ctest (Linux inclus).
//
// Convention : toutes les dates sont des dates CIVILES UTC (le serveur ne
// gère aucun fuseau). Le 29 février est fêté le 28/02 les années non
// bissextiles.

#include <cstdint>
#include <string_view>

namespace engine::anniversary
{
	/// Date civile (année/mois/jour), sans heure ni fuseau.
	struct YmdDate
	{
		int year  = 0; ///< ex. 2026
		int month = 0; ///< 1..12
		int day   = 0; ///< 1..31
	};

	/// true si \p year est bissextile (calendrier grégorien).
	bool IsLeapYear(int year);

	/// Parse STRICT du format "yyyy-mm-dd" (10 caractères exactement, chiffres
	/// et tirets aux bonnes positions) et vérifie que la date existe dans le
	/// calendrier (mois 1-12, jour borné par le mois, 29/02 seulement si
	/// bissextile). \return false si le format ou la date est invalide.
	bool ParseYmd(std::string_view text, YmdDate& out);

	/// Validation d'une date de naissance à l'inscription : date parsable,
	/// année >= 1900, et pas dans le futur par rapport à \p todayUtc.
	/// (Aucune exigence d'âge minimum ici — le contrôle parental existant
	/// reste la référence pour ça.)
	bool IsValidBirthDate(std::string_view text, const YmdDate& todayUtc);

	/// Années RÉVOLUES entre \p from et \p today (0 si moins d'un an, ou si
	/// today < from). Ex. inscrit le 2024-07-18 : 1 le 2025-07-18, encore 1
	/// le 2026-07-17, 2 le 2026-07-18. Le 29/02 compte comme révolu le 28/02
	/// des années non bissextiles.
	int YearsElapsed(const YmdDate& from, const YmdDate& today);

	/// true si \p today est le jour d'anniversaire de \p birth : même
	/// jour/mois, avec la règle 29/02 → 28/02 les années non bissextiles.
	/// L'année de naissance elle-même ne compte pas (pas d'anniversaire le
	/// jour de l'inscription d'un nouveau-né du jour).
	bool IsAnniversaryDay(const YmdDate& birth, const YmdDate& today);

	/// Date civile UTC du jour, depuis l'horloge système. Séparée pour que
	/// les tests injectent leurs propres dates sans toucher à l'horloge.
	YmdDate TodayUtc();

	/// Décale \p date de \p days jours civils (positif ou négatif),
	/// calendrier grégorien (algorithme days-from-civil de Hinnant). Pur.
	YmdDate AddDays(const YmdDate& date, int days);

	/// Convertit l'instant UTC \p utcEpochSeconds décalé de \p offsetMinutes
	/// en date civile LOCALE + heure locale (0..23). Pur (aucune horloge) —
	/// sert à l'envoi « à 7 h du matin chez le joueur » (BirthdayEmailJob).
	void LocalCivilFromUtc(int64_t utcEpochSeconds, int offsetMinutes,
		YmdDate& outDate, int& outHour);
}
