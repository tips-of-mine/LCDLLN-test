// ETAPE4.2 — tests de l'étalement des travaux périodiques (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/TickSlicing.h"

#include <cstdio>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}

	using namespace engine::server::tick_slicing;
}

int main()
{
	// 1. Période en ticks.
	Check(PeriodTicks(20u, 1.0) == 20u, "1 Hz à 20 Hz : 20 ticks");
	Check(PeriodTicks(20u, 2.0) == 10u, "2 Hz : 10 ticks");
	Check(PeriodTicks(20u, 4.0) == 5u, "4 Hz : 5 ticks");
	Check(PeriodTicks(20u, 20.0) == 1u && PeriodTicks(20u, 50.0) == 1u, "≥ tick : chaque tick");
	Check(PeriodTicks(20u, 0.0) == 1u && PeriodTicks(20u, -3.0) == 1u, "≤ 0 : chaque tick");
	Check(PeriodTicks(0u, 1.0) == 1u, "tick 0 Hz : pas de division par zéro");
	Check(PeriodTicks(20u, 0.1) == 200u, "0,1 Hz : 200 ticks");

	// 2. Chaque objet exactement une fois par période, quel que soit le tick de départ.
	for (uint32_t period : { 1u, 5u, 10u, 20u, 37u })
	{
		bool once = true;
		for (uint64_t id = 0; id < 500; ++id)
		{
			uint32_t hits = 0;
			for (uint32_t t = 123456u; t < 123456u + period; ++t)
				hits += IsSliceTurn(id, t, period) ? 1u : 0u;
			once = once && hits == 1u;
		}
		Check(once, "une fois par période");
	}

	// 3. Charge répartie : 1 000 spawners à 2 Hz (10 ticks) → 100 par tick, sans pic.
	{
		std::vector<uint32_t> perTick(10u, 0u);
		for (uint64_t id = 0; id < 1000; ++id)
			for (uint32_t t = 0; t < 10u; ++t)
				perTick[t] += IsSliceTurn(id, 5000u + t, 10u) ? 1u : 0u;
		bool even = true;
		for (const uint32_t n : perTick)
			even = even && n == 100u;
		Check(even, "1 000 objets sur 10 ticks : 100 par tick");
	}

	// 4. Période 0 traitée comme 1 (chaque tick).
	Check(IsSliceTurn(42u, 7u, 0u), "période 0 : chaque tick");

	if (s_failCount == 0)
		std::puts("[OK] tick_slicing_tests");
	return s_failCount == 0 ? 0 : 1;
}
