/// Tests unitaires CPU pour AnniversaryMath (spec 2026-07-18) : parse strict
/// yyyy-mm-dd, validation de date de naissance, années révolues (dont 29/02),
/// jour d'anniversaire (dont règle 29/02 → 28/02). Pur CPU, ctest.

#include "src/shared/anniversary/AnniversaryMath.h"

#include <cstdio>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using namespace engine::anniversary;

	YmdDate D(int y, int m, int d) { return YmdDate{ y, m, d }; }

	void Test_ParseYmd()
	{
		YmdDate out{};
		REQUIRE(ParseYmd("1990-05-21", out));
		REQUIRE(out.year == 1990 && out.month == 5 && out.day == 21);
		REQUIRE(ParseYmd("2024-02-29", out));   // bissextile
		REQUIRE(!ParseYmd("2023-02-29", out));  // non bissextile
		REQUIRE(!ParseYmd("1990-13-01", out));  // mois invalide
		REQUIRE(!ParseYmd("1990-04-31", out));  // jour invalide
		REQUIRE(!ParseYmd("1990-00-10", out));
		REQUIRE(!ParseYmd("1990-5-21", out));   // format non zero-padded
		REQUIRE(!ParseYmd("21/05/1990", out));  // mauvais séparateurs
		REQUIRE(!ParseYmd("1990-05-21x", out)); // trop long
		REQUIRE(!ParseYmd("", out));
	}

	void Test_IsValidBirthDate()
	{
		const YmdDate today = D(2026, 7, 18);
		REQUIRE(IsValidBirthDate("1990-05-21", today));
		REQUIRE(IsValidBirthDate("2026-07-18", today));  // aujourd'hui : accepté
		REQUIRE(!IsValidBirthDate("2026-07-19", today)); // futur
		REQUIRE(!IsValidBirthDate("1899-12-31", today)); // < 1900
		REQUIRE(IsValidBirthDate("1900-01-01", today));
		REQUIRE(!IsValidBirthDate("n/a", today));
	}

	void Test_YearsElapsed()
	{
		// Inscrit le 2024-07-18.
		const YmdDate from = D(2024, 7, 18);
		REQUIRE(YearsElapsed(from, D(2024, 12, 31)) == 0);
		REQUIRE(YearsElapsed(from, D(2025, 7, 17)) == 0);
		REQUIRE(YearsElapsed(from, D(2025, 7, 18)) == 1); // jour anniversaire compte
		REQUIRE(YearsElapsed(from, D(2026, 7, 17)) == 1);
		REQUIRE(YearsElapsed(from, D(2026, 7, 18)) == 2);
		REQUIRE(YearsElapsed(from, D(2020, 1, 1)) == 0);  // avant l'inscription

		// Inscrit un 29/02 : révolu le 28/02 des années non bissextiles.
		const YmdDate leap = D(2024, 2, 29);
		REQUIRE(YearsElapsed(leap, D(2025, 2, 27)) == 0);
		REQUIRE(YearsElapsed(leap, D(2025, 2, 28)) == 1);
		REQUIRE(YearsElapsed(leap, D(2028, 2, 28)) == 3);
		REQUIRE(YearsElapsed(leap, D(2028, 2, 29)) == 4); // bissextile : vrai jour
	}

	void Test_IsAnniversaryDay()
	{
		const YmdDate birth = D(1990, 5, 21);
		REQUIRE(IsAnniversaryDay(birth, D(2026, 5, 21)));
		REQUIRE(!IsAnniversaryDay(birth, D(2026, 5, 20)));
		REQUIRE(!IsAnniversaryDay(birth, D(2026, 6, 21)));
		REQUIRE(!IsAnniversaryDay(birth, D(1990, 5, 21))); // année de naissance

		// 29/02 → fêté le 28/02 hors bissextile, le 29/02 sinon.
		const YmdDate leapBirth = D(2000, 2, 29);
		REQUIRE(IsAnniversaryDay(leapBirth, D(2023, 2, 28)));
		REQUIRE(!IsAnniversaryDay(leapBirth, D(2023, 3, 1)));
		REQUIRE(IsAnniversaryDay(leapBirth, D(2024, 2, 29)));
		REQUIRE(!IsAnniversaryDay(leapBirth, D(2024, 2, 28)));
	}

	void Test_AddDays()
	{
		const YmdDate d = D(2026, 7, 18);
		const YmdDate p = AddDays(d, 1);
		REQUIRE(p.year == 2026 && p.month == 7 && p.day == 19);
		const YmdDate m = AddDays(d, -1);
		REQUIRE(m.year == 2026 && m.month == 7 && m.day == 17);
		// Franchissements : fin de mois, fin d'année, 29/02.
		const YmdDate eom = AddDays(D(2026, 1, 31), 1);
		REQUIRE(eom.year == 2026 && eom.month == 2 && eom.day == 1);
		const YmdDate eoy = AddDays(D(2025, 12, 31), 1);
		REQUIRE(eoy.year == 2026 && eoy.month == 1 && eoy.day == 1);
		const YmdDate leap = AddDays(D(2024, 2, 28), 1);
		REQUIRE(leap.year == 2024 && leap.month == 2 && leap.day == 29);
	}

	void Test_LocalCivilFromUtc()
	{
		// 2026-07-18 05:30:00 UTC = epoch 1784352600.
		const int64_t t = 1784352600ll;
		YmdDate date{}; int hour = 0;
		LocalCivilFromUtc(t, 0, date, hour);
		REQUIRE(date.year == 2026 && date.month == 7 && date.day == 18);
		REQUIRE(hour == 5);
		// UTC+2 (été FR) : 07:30 locales — le seuil 7 h est atteint.
		LocalCivilFromUtc(t, 120, date, hour);
		REQUIRE(date.day == 18 && hour == 7);
		// UTC-6 (US Central) : encore la VEILLE 23:30 locales.
		LocalCivilFromUtc(t, -360, date, hour);
		REQUIRE(date.day == 17 && hour == 23);
		// UTC+12 : déjà 17:30 locales le 18.
		LocalCivilFromUtc(t, 720, date, hour);
		REQUIRE(date.day == 18 && hour == 17);
		// Demi-heure (Inde, +5:30) : 11:00 locales.
		LocalCivilFromUtc(t, 330, date, hour);
		REQUIRE(date.day == 18 && hour == 11);
	}

	void Test_IsLeapYear()
	{
		REQUIRE(IsLeapYear(2024));
		REQUIRE(!IsLeapYear(2023));
		REQUIRE(IsLeapYear(2000));  // divisible par 400
		REQUIRE(!IsLeapYear(1900)); // divisible par 100, pas 400
	}
}

int main()
{
	Test_ParseYmd();
	Test_IsValidBirthDate();
	Test_YearsElapsed();
	Test_IsAnniversaryDay();
	Test_AddDays();
	Test_LocalCivilFromUtc();
	Test_IsLeapYear();

	if (g_failed == 0)
	{
		std::printf("[PASS] AnniversaryMathTests\n");
		return 0;
	}
	std::printf("[FAIL] AnniversaryMathTests: %d failure(s)\n", g_failed);
	return 1;
}
