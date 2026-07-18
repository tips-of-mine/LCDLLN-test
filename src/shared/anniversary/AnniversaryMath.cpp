// AnniversaryMath — implémentation. Voir le header pour les conventions
// (dates civiles UTC, règle 29/02 → 28/02).

#include "src/shared/anniversary/AnniversaryMath.h"

#include <ctime>

namespace engine::anniversary
{
	bool IsLeapYear(int year)
	{
		return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
	}

	namespace
	{
		/// Nombre de jours du mois \p month (1..12) de l'année \p year.
		int DaysInMonth(int year, int month)
		{
			static constexpr int kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
			if (month < 1 || month > 12) return 0;
			if (month == 2 && IsLeapYear(year)) return 29;
			return kDays[month - 1];
		}

		/// true si les 10 caractères de \p s ont la forme "dddd-dd-dd".
		bool HasYmdShape(std::string_view s)
		{
			if (s.size() != 10) return false;
			for (size_t i = 0; i < 10; ++i)
			{
				if (i == 4 || i == 7)
				{
					if (s[i] != '-') return false;
				}
				else if (s[i] < '0' || s[i] > '9')
				{
					return false;
				}
			}
			return true;
		}

		/// Jour/mois effectifs où l'anniversaire de \p birth est fêté l'année
		/// \p inYear : identiques à la naissance, sauf 29/02 → 28/02 si
		/// \p inYear n'est pas bissextile.
		void CelebratedDayMonth(const YmdDate& birth, int inYear, int& outMonth, int& outDay)
		{
			outMonth = birth.month;
			outDay   = birth.day;
			if (birth.month == 2 && birth.day == 29 && !IsLeapYear(inYear))
				outDay = 28;
		}
	}

	bool ParseYmd(std::string_view text, YmdDate& out)
	{
		if (!HasYmdShape(text)) return false;
		const auto digit = [&](size_t i) { return static_cast<int>(text[i] - '0'); };
		const int y = digit(0) * 1000 + digit(1) * 100 + digit(2) * 10 + digit(3);
		const int m = digit(5) * 10 + digit(6);
		const int d = digit(8) * 10 + digit(9);
		if (m < 1 || m > 12) return false;
		if (d < 1 || d > DaysInMonth(y, m)) return false;
		out.year = y; out.month = m; out.day = d;
		return true;
	}

	bool IsValidBirthDate(std::string_view text, const YmdDate& todayUtc)
	{
		YmdDate b{};
		if (!ParseYmd(text, b)) return false;
		if (b.year < 1900) return false;
		// Pas dans le futur (comparaison lexicographique (y, m, d)).
		if (b.year > todayUtc.year) return false;
		if (b.year == todayUtc.year && b.month > todayUtc.month) return false;
		if (b.year == todayUtc.year && b.month == todayUtc.month && b.day > todayUtc.day) return false;
		return true;
	}

	int YearsElapsed(const YmdDate& from, const YmdDate& today)
	{
		if (from.year <= 0) return 0;
		int years = today.year - from.year;
		if (years <= 0) return 0;
		// L'anniversaire de cette année est-il déjà passé (ou aujourd'hui) ?
		int celMonth = 0, celDay = 0;
		CelebratedDayMonth(from, today.year, celMonth, celDay);
		if (today.month < celMonth || (today.month == celMonth && today.day < celDay))
			--years;
		return years < 0 ? 0 : years;
	}

	bool IsAnniversaryDay(const YmdDate& birth, const YmdDate& today)
	{
		if (birth.year <= 0 || today.year <= birth.year) return false;
		int celMonth = 0, celDay = 0;
		CelebratedDayMonth(birth, today.year, celMonth, celDay);
		return today.month == celMonth && today.day == celDay;
	}

	YmdDate TodayUtc()
	{
		std::time_t now = std::time(nullptr);
		std::tm tmv{};
#if defined(_WIN32)
		gmtime_s(&tmv, &now);
#else
		gmtime_r(&now, &tmv);
#endif
		YmdDate d;
		d.year  = tmv.tm_year + 1900;
		d.month = tmv.tm_mon + 1;
		d.day   = tmv.tm_mday;
		return d;
	}
}
