// src/shared/world/tests/WorldClockTests.cpp
#include "src/shared/world/WorldClock.h"
#include <cstdio>
#include <cmath>

using engine::world::WorldClockParams;
using engine::world::GameSeconds;
using engine::world::TimeOfDayHours;
using engine::world::LunarPhase;
using engine::world::DayPhase;
using engine::world::DayPhaseAt;

static int g_failed = 0;
#define CHECK(cond) do { if(!(cond)){ std::fprintf(stderr,"[FAIL] %s:%d %s\n",__FILE__,__LINE__,#cond); ++g_failed; } } while(0)
static bool nearly(double a, double b, double eps=1e-3){ return std::fabs(a-b) < eps; }

int main()
{
    WorldClockParams p;
    p.epochRefUnixMs = 1000ull;
    p.timeScaleRealMinPerDay = 60.0f;   // 1 s reelle = 24 s de jeu

    CHECK(nearly(GameSeconds(1000ull, p), 0.0));
    CHECK(nearly(TimeOfDayHours(0.0), 0.0));
    CHECK(nearly(GameSeconds(2000ull, p), 24.0));
    CHECK(nearly(GameSeconds(1000ull + 3600ull*1000ull, p), 86400.0, 1.0));
    CHECK(nearly(TimeOfDayHours(86400.0), 0.0));

    p.offsetGameSec = 43200.0;
    CHECK(nearly(TimeOfDayHours(GameSeconds(1000ull, p)), 12.0));

    WorldClockParams pp; pp.paused = true; pp.pausedAtGameSec = 7200.0;
    CHECK(nearly(GameSeconds(999999ull, pp), 7200.0));
    CHECK(nearly(TimeOfDayHours(7200.0), 2.0));

    const double period = 16.0*86400.0;
    CHECK(LunarPhase(0.0, period) == 0);
    CHECK(LunarPhase(period*0.5 + 1.0, period) == 8);
    CHECK(LunarPhase(period - 1.0, period) == 15);

    CHECK(DayPhaseAt(0.0f)  == DayPhase::Night);
    CHECK(DayPhaseAt(3.0f)  == DayPhase::Night);
    CHECK(DayPhaseAt(6.0f)  == DayPhase::Dawn);
    CHECK(DayPhaseAt(12.0f) == DayPhase::Day);
    CHECK(DayPhaseAt(20.0f) == DayPhase::Dusk);
    CHECK(DayPhaseAt(23.0f) == DayPhase::Night);

    if (g_failed == 0) std::printf("[OK] WorldClockTests\n");
    return g_failed;
}
