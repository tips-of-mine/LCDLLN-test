// ETAPE4.4 — tests de l'agenda déterministe des PNJ (sans réseau).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/gameplay/ambient/DailyAgenda.h"

#include <cmath>
#include <cstdio>

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

	using namespace engine::server::ambient;

	constexpr double kHour = 3600.0;
	constexpr double kDay = 86400.0;
	constexpr double kRate = 24.0;   // un jour de jeu = une heure réelle
	constexpr float kWalk = 1.4f;    // m/s

	RoutinePoint Pt(float x, float z)
	{
		RoutinePoint p;
		p.x = x;
		p.z = z;
		return p;
	}

	AgendaDefinition Artisan()
	{
		AgendaDefinition def;
		def.enabled = true;
		def.profession = "artisan";
		def.places[static_cast<size_t>(AgendaPlace::Home)] = { Pt(0.0f, 0.0f) };
		def.places[static_cast<size_t>(AgendaPlace::Work)] = { Pt(100.0f, 0.0f) };
		def.places[static_cast<size_t>(AgendaPlace::Tavern)] = { Pt(0.0f, 100.0f), Pt(0.0f, -100.0f) };
		return def;
	}

	float Dist(RoutinePoint a, RoutinePoint b)
	{
		return std::hypot(a.x - b.x, a.z - b.z);
	}
}

int main()
{
	// 0. Toutes les routines intégrées : triées, premier créneau à 0 h identique au dernier.
	for (const std::string_view id : ProfessionIds())
	{
		const std::vector<AgendaSlot>* slots = FindProfession(id);
		bool ok = slots != nullptr && slots->size() >= 3u && slots->front().startHour == 0.0f
			&& slots->front().place == slots->back().place && slots->front().activity == slots->back().activity;
		for (size_t i = 1; ok && i < slots->size(); ++i)
			ok = (*slots)[i].startHour > (*slots)[i - 1].startHour && (*slots)[i].startHour < 24.0f;
		Check(ok, "routine intégrée cohérente (triée, nuit qui traverse minuit)");
	}
	Check(FindProfession("inconnu") == nullptr, "métier inconnu");
	AgendaPlace place{};
	Check(ParseAgendaPlace("tavern", place) && place == AgendaPlace::Tavern && !ParseAgendaPlace("cave", place), "lieux JSON");

	const AgendaDefinition def = Artisan();
	const uint64_t seed = AgendaSeed("N003_W009_amb_marn_villageois_4", 0u);
	const RoutinePoint fallback = Pt(5.0f, 5.0f);
	const double day = 12.0 * kDay; // jour de jeu 12

	// 1. Nuit : chez lui, invisible.
	{
		const AgendaState s = EvaluateAgenda(def, seed, day + 3.0 * kHour, kWalk, kRate, fallback);
		Check(s.activity == AgendaActivity::Sleep && s.indoors && s.place == AgendaPlace::Home, "3 h : dort chez lui");
		Check(Dist(s.position, Pt(0.0f, 0.0f)) < 0.01f, "position = domicile");
	}

	// 2. Matinée : au travail ; juste après le début du créneau : en route.
	{
		const AgendaState s = EvaluateAgenda(def, seed, day + 9.0 * kHour, kWalk, kRate, fallback);
		Check(s.activity == AgendaActivity::Work && s.place == AgendaPlace::Work && !s.indoors && !s.travelling, "9 h : au travail");
		Check(Dist(s.position, Pt(100.0f, 0.0f)) < 2.0f, "position = atelier (à 1,5 m près)");
		const AgendaState early = EvaluateAgenda(def, seed, s.slotStartGameSeconds + 60.0, kWalk, kRate, fallback);
		Check(early.travelling && early.position.x > 0.5f && early.position.x < 99.0f, "début du créneau : en route vers l'atelier");
		const double travel = 100.0 / kWalk * kRate; // ≈ 1 714 s de jeu
		const AgendaState arrived = EvaluateAgenda(def, seed, s.slotStartGameSeconds + travel + 10.0, kWalk, kRate, fallback);
		Check(!arrived.travelling && Dist(arrived.position, arrived.target) < 0.01f, "arrivé à l'atelier après le trajet");
	}

	// 3. Déterminisme : même graine, même instant → même état ; autre PNJ → autre variante.
	{
		const AgendaState a = EvaluateAgenda(def, seed, day + 12.2 * kHour, kWalk, kRate, fallback);
		const AgendaState b = EvaluateAgenda(def, seed, day + 12.2 * kHour, kWalk, kRate, fallback);
		Check(a.slotIndex == b.slotIndex && a.position.x == b.position.x && a.position.z == b.position.z, "déterministe");
		bool differs = false;
		for (uint32_t k = 1; k < 40u && !differs; ++k)
		{
			const uint64_t other = AgendaSeed("N003_W009_amb_marn_villageois_4", k);
			const AgendaState o = EvaluateAgenda(def, other, day + 6.5 * kHour + 5.0 * 60.0, kWalk, kRate, fallback);
			const AgendaState m = EvaluateAgenda(def, seed, day + 6.5 * kHour + 5.0 * 60.0, kWalk, kRate, fallback);
			differs = o.slotIndex != m.slotIndex || std::fabs(o.slotStartGameSeconds - m.slotStartGameSeconds) > 1.0;
		}
		Check(differs, "horaires décalés d'un PNJ à l'autre");
	}

	// 4. Décalage borné : jamais plus de jitterMinutes, ordre des créneaux préservé.
	{
		bool bounded = true;
		for (uint32_t k = 0; k < 200u; ++k)
		{
			const uint64_t s = AgendaSeed("borne", k);
			for (int d = 0; d < 5; ++d)
			{
				// 6 h 30 ± 20 min : à 6 h 09 jamais au travail, à 6 h 51 toujours parti de chez lui.
				const AgendaState before = EvaluateAgenda(def, s, (100 + d) * kDay + 6.15 * kHour, kWalk, kRate, fallback);
				const AgendaState after = EvaluateAgenda(def, s, (100 + d) * kDay + 6.85 * kHour, kWalk, kRate, fallback);
				bounded = bounded && before.place == AgendaPlace::Home && after.place == AgendaPlace::Work;
			}
		}
		Check(bounded, "décalage ≤ 20 min sur 1 000 journées");
	}

	// 5. Auberge choisie chaque jour parmi plusieurs ; domicile et atelier fixes.
	{
		bool sawNorth = false;
		bool sawSouth = false;
		for (int d = 0; d < 60; ++d)
		{
			const AgendaState s = EvaluateAgenda(def, seed, d * kDay + 12.7 * kHour, kWalk, kRate, fallback);
			if (s.place != AgendaPlace::Tavern || s.travelling) continue;
			sawNorth = sawNorth || s.target.z > 50.0f;
			sawSouth = sawSouth || s.target.z < -50.0f;
		}
		Check(sawNorth && sawSouth, "auberge variée selon le jour");
		const AgendaState w1 = EvaluateAgenda(def, seed, 3 * kDay + 10.0 * kHour, kWalk, kRate, fallback);
		const AgendaState w2 = EvaluateAgenda(def, seed, 40 * kDay + 10.0 * kHour, kWalk, kRate, fallback);
		Check(Dist(w1.target, w2.target) < 0.01f, "atelier fixe");
	}

	// 6. Rentre chez lui le soir : visible en route, invisible une fois arrivé.
	{
		const AgendaState s = EvaluateAgenda(def, seed, day + 21.5 * kHour, kWalk, kRate, fallback);
		Check(s.activity == AgendaActivity::Sleep, "21 h 30 : nuit");
		const AgendaState walking = EvaluateAgenda(def, seed, s.slotStartGameSeconds + 30.0, kWalk, kRate, fallback);
		Check(walking.travelling && !walking.indoors, "début de nuit : marche vers chez lui, visible");
		const AgendaState home = EvaluateAgenda(def, seed, day + 23.9 * kHour, kWalk, kRate, fallback);
		Check(home.indoors, "plus tard : chez lui, invisible");
	}

	// 7. Lieu sans point : domicile, puis point de repli ; agenda désactivé : repli.
	{
		AgendaDefinition bare;
		bare.enabled = true;
		bare.profession = "flaneur";
		const AgendaState s = EvaluateAgenda(bare, seed, day + 9.0 * kHour, kWalk, kRate, fallback);
		Check(Dist(s.position, fallback) < 0.01f, "aucun point : position de repli");
		AgendaDefinition off = def;
		off.enabled = false;
		const AgendaState o = EvaluateAgenda(off, seed, day + 9.0 * kHour, kWalk, kRate, fallback);
		Check(o.activity == AgendaActivity::Rest && !o.indoors && Dist(o.position, fallback) < 0.01f, "désactivé : au repos sur le repli");
		const AgendaState n = EvaluateAgenda(def, seed, std::nan(""), kWalk, kRate, fallback);
		Check(Dist(n.position, fallback) < 0.01f, "instant NaN : repli");
	}

	// 8. Flânerie : point qui change avec le temps, borné, déterministe.
	{
		AgendaDefinition f = def;
		f.profession = "flaneur";
		f.places[static_cast<size_t>(AgendaPlace::Square)] = { Pt(50.0f, 50.0f) };
		f.strollRadiusMeters = 4.0f;
		const double t = day + 9.0 * kHour;
		const AgendaState s = EvaluateAgenda(f, seed, t, kWalk, kRate, fallback);
		Check(s.activity == AgendaActivity::Stroll, "9 h : flânerie sur la place");
		const RoutinePoint a = StrollPoint(f, seed, s, t);
		const RoutinePoint b = StrollPoint(f, seed, s, t + 600.0);
		Check(Dist(a, s.target) <= 4.01f && Dist(b, s.target) <= 4.01f, "flânerie dans le rayon");
		Check(Dist(a, b) > 0.01f, "le point de flânerie change");
		Check(Dist(a, StrollPoint(f, seed, s, t)) < 1e-6f, "flânerie déterministe");
		const AgendaState w = EvaluateAgenda(def, seed, day + 9.0 * kHour, kWalk, kRate, fallback);
		Check(Dist(StrollPoint(def, seed, w, t), w.target) < 1e-6f, "hors flânerie : point d'arrêt");
	}

	if (s_failCount == 0)
		std::puts("[OK] daily_agenda_tests");
	return s_failCount == 0 ? 0 : 1;
}
