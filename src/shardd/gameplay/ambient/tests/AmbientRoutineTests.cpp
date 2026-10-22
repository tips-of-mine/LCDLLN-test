// ambient_routine_tests — logique pure des routines des PNJ d'ambiance.
#include "src/shardd/gameplay/ambient/AmbientRoutine.h"

#include <cmath>
#include <cstdio>

using namespace engine::server::ambient;

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}
	/// Générateur déterministe (LCG) passé en contexte.
	float Roll(void* ctx)
	{
		auto* s = static_cast<uint32_t*>(ctx);
		*s = *s * 1664525u + 1013904223u;
		return static_cast<float>(*s >> 8) / 16777216.0f;
	}
}

int main()
{
	// Libellés.
	RoutineKind k{};
	Check(ParseRoutineKind("tour", k) && k == RoutineKind::Tour, "parse tour");
	Check(ParseRoutineKind("patrol", k) && k == RoutineKind::Patrol, "parse patrol");
	Check(!ParseRoutineKind("vol", k) && k == RoutineKind::None, "kind inconnu rejete");
	RoutineSchedule sc{};
	Check(ParseRoutineSchedule("night", sc) && sc == RoutineSchedule::Night, "parse night");
	Check(!ParseRoutineSchedule("midi", sc), "horaire inconnu rejete");

	// Horaires : jour 6h-21h, nuit 20h-6h, toujours.
	Check(IsScheduleOpen(RoutineSchedule::Day, 12.0f) && !IsScheduleOpen(RoutineSchedule::Day, 23.0f), "horaire jour");
	Check(IsScheduleOpen(RoutineSchedule::Night, 2.0f) && !IsScheduleOpen(RoutineSchedule::Night, 12.0f), "horaire nuit");
	Check(IsScheduleOpen(RoutineSchedule::Night, 20.5f) && IsScheduleOpen(RoutineSchedule::Day, 20.5f), "releve au crepuscule");
	Check(IsScheduleOpen(RoutineSchedule::Always, 3.0f), "toujours");

	uint32_t seed = 7u;
	// Boucle : 0 → 1 → 2 → 0.
	{
		RoutineDefinition d;
		d.kind = RoutineKind::Tour;
		d.path = { { 0, 0 }, { 10, 0 }, { 10, 10 } };
		RoutineState st;
		ChooseNextTarget(d, st, 0, 0, Roll, &seed);
		Check(st.waypoint == 0 && st.target.x == 0.0f, "tour : depart a la porte");
		ChooseNextTarget(d, st, 0, 0, Roll, &seed);
		Check(st.waypoint == 1 && st.target.x == 10.0f, "tour : point suivant");
		ChooseNextTarget(d, st, 0, 0, Roll, &seed);
		ChooseNextTarget(d, st, 0, 0, Roll, &seed);
		Check(st.waypoint == 0, "tour : bouclage");
		Check(HomePoint(d, 5, 5).x == 0.0f, "porte = premier point");
	}
	// Aller-retour : 0 1 2 1 0 1.
	{
		RoutineDefinition d;
		d.kind = RoutineKind::Patrol;
		d.pingPong = true;
		d.path = { { 0, 0 }, { 1, 0 }, { 2, 0 } };
		RoutineState st;
		const uint32_t expect[] = { 0, 1, 2, 1, 0, 1 };
		bool ok = true;
		for (uint32_t e : expect)
		{
			ChooseNextTarget(d, st, 0, 0, Roll, &seed);
			ok = ok && st.waypoint == e;
		}
		Check(ok, "patrol pingPong : 0 1 2 1 0 1");
	}
	// Poste : errance bornée par le rayon autour du poste.
	{
		RoutineDefinition d;
		d.kind = RoutineKind::Post;
		d.wanderRadiusMeters = 1.5f;
		d.path = { { 100, -50 } };
		RoutineState st;
		bool inside = true;
		for (int i = 0; i < 200; ++i)
		{
			ChooseNextTarget(d, st, 0, 0, Roll, &seed);
			inside = inside && std::hypot(st.target.x - 100.0f, st.target.z + 50.0f) <= 1.5001f;
		}
		Check(inside, "post : errance dans le rayon");
		// Poste : toujours une pause, dans [min, max].
		d.pauseMinSec = 4.0f; d.pauseMaxSec = 6.0f;
		bool pauseOk = true;
		for (int i = 0; i < 100; ++i)
		{
			const float p = RollPauseSeconds(d, Roll, &seed);
			pauseOk = pauseOk && p >= 4.0f && p <= 6.0f;
		}
		Check(pauseOk, "post : pause systematique bornee");
	}
	// Tournée : chance de pause respectée en moyenne (~35 %).
	{
		RoutineDefinition d;
		d.kind = RoutineKind::Tour;
		d.pauseChance = 0.35f; d.pauseMinSec = 1.0f; d.pauseMaxSec = 2.0f;
		int pauses = 0;
		for (int i = 0; i < 4000; ++i) if (RollPauseSeconds(d, Roll, &seed) > 0.0f) ++pauses;
		Check(pauses > 1200 && pauses < 1600, "tour : frequence de pause ~35 %");
	}
	if (g_fail == 0) std::printf("AmbientRoutineTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
