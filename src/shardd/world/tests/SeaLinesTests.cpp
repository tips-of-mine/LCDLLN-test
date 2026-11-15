// sea_lines_tests — v1.0.124 : lignes maritimes ; v1.0.125 : bateaux (horaire, pose, pont).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/SeaLines.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}
	bool Near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) <= eps; }

	// Schéma 1 (v1.0.124) : pontons seuls, aucun bateau.
	const char* kJsonV1 = R"({
  "schema_version": 1, "world": "royaumes",
  "lines": [
    { "id": "navette", "name": "Navette", "loop": false, "price_gold_per_leg": 5, "stops": [
      { "city": "e", "cell": "N001_E000", "x": 0, "y": 40, "z": 5000 },
      { "city": "f", "cell": "N002_E000", "x": 0, "y": 40, "z": 9000 } ] },
    { "id": "sans_ponton", "loop": false, "price_gold_per_leg": 5, "stops": [
      { "city": "g", "cell": "N001_E000", "x": 0, "z": 0 },
      { "city": "h", "cell": "N002_E000" } ] }
  ]
})";

	// Schéma 2 (v1.0.125) : boucle A (0,0) → B (1000,0) → C (1000,1000) → A, 2 bateaux ;
	// aller-retour D (0,5000) → E (0,6000) → F (0,8000), 1 bateau ; ligne à étape manquante.
	const char* kJsonV2 = R"({
  "schema_version": 2, "world": "royaumes", "sea_level_m": 40.0,
  "lines": [
    { "id": "boucle", "name": "Boucle", "loop": true, "price_gold_per_leg": 10, "boats": 2, "stop_seconds": 30, "stops": [
      { "city": "a", "name": "A", "cell": "N000_E000", "x": -30, "y": 41, "z": 0, "yaw_deg": 90, "dock_x": 0, "dock_z": 0, "dock_yaw_deg": 0 },
      { "city": "b", "name": "B", "cell": "N000_E000", "x": 1030, "y": 42, "z": 0, "dock_x": 1000, "dock_z": 0, "dock_yaw_deg": 80 },
      { "city": "c", "name": "C", "cell": "N000_E000", "x": 1030, "y": 43, "z": 1000, "dock_x": 1000, "dock_z": 1000, "dock_yaw_deg": 90 } ],
      "legs": [
      { "from": 0, "to": 1, "points": [[0, 0], [500, 0], [1000, 0]] },
      { "from": 1, "to": 2, "points": [[1000, 0], [1000, 1000]] },
      { "from": 2, "to": 0, "points": [[1000, 1000], [0, 0]] } ] },
    { "id": "aller_retour", "loop": false, "price_gold_per_leg": 10, "boats": 1, "stop_seconds": 20, "stops": [
      { "city": "d", "cell": "N000_E000", "x": 0, "y": 40, "z": 5000, "dock_x": 0, "dock_z": 5000, "dock_yaw_deg": 90 },
      { "city": "e", "cell": "N000_E000", "x": 0, "y": 40, "z": 6000, "dock_x": 0, "dock_z": 6000, "dock_yaw_deg": 90 },
      { "city": "f", "cell": "N000_E000", "x": 0, "y": 40, "z": 8000, "dock_x": 0, "dock_z": 8000, "dock_yaw_deg": 90 } ],
      "legs": [
      { "from": 0, "to": 1, "points": [[0, 5000], [0, 6000]] },
      { "from": 2, "to": 1, "points": [[0, 8000], [0, 6000]] } ] },
    { "id": "incomplete", "loop": true, "boats": 2, "stops": [
      { "city": "g", "cell": "N000_E000", "x": 0, "y": 40, "z": 0, "dock_x": 0, "dock_z": 0 },
      { "city": "h", "cell": "N000_E000", "x": 0, "y": 40, "z": 100, "dock_x": 0, "dock_z": 100 },
      { "city": "i", "cell": "N000_E000", "x": 0, "y": 40, "z": 200, "dock_x": 0, "dock_z": 200 } ],
      "legs": [ { "from": 0, "to": 1, "points": [[0, 0], [0, 100]] } ] }
  ]
})";
}

int main()
{
	using namespace engine::server;
	constexpr double kPi = 3.14159265358979323846;
	std::string err;
	{
		SeaLines s;
		Check(!s.AddFromText("{", err), "JSON invalide refuse");
		Check(!s.AddFromText(R"({"schema_version": 3, "world": "royaumes", "lines": []})", err), "schema 3 refuse");
		Check(s.AddFromText(kJsonV1, err) && s.Count() == 1, "schema 1 : 1 ligne chargee (escale sans ponton ignoree)");
		const SeaLine* n = s.Find("navette");
		Check(n && n->stops.size() == 2 && n->boats == 0u, "schema 1 : pas de bateau");
		BoatFleet f;
		f.Build(s, 8.0);
		Check(f.Count() == 0, "schema 1 : flotte vide");
	}

	SeaLines s;
	Check(s.AddFromText(kJsonV2, err) && s.Count() == 3, "schema 2 : 3 lignes chargees");
	const SeaLine* b = s.Find("boucle");
	Check(b && b->boats == 2u && b->legs.size() == 3 && Near(b->stopSeconds, 30.0) && Near(b->stops[1].dockX, 1000.0)
		&& Near(b->seaLevelM, 40.0), "boucle : 2 bateaux, 3 etapes, quais");
	const SeaLine* inc = s.Find("incomplete");
	Check(inc && inc->boats == 0u, "etape manquante : pas de bateau");

	BoatFleet f;
	f.Build(s, 10.0);
	Check(f.Count() == 3, "flotte : 2 + 1 bateaux");
	// Boucle : 3 arrêts de 30 s + (1000 + 1000 + 1414,2) m à 10 m/s.
	const double period = 90.0 + (2000.0 + std::sqrt(2.0) * 1000.0) / 10.0;
	Check(Near(f.CycleSeconds(0), period, 1e-6) && f.RankOf(1) == 1u && f.LineOf(1).id == "boucle", "boucle : periode du cycle");
	BoatPose p = f.Evaluate(0, 0.0);
	Check(p.docked && p.stop == 0 && Near(p.x, 0.0) && Near(p.secondsLeft, 30.0) && p.departures == 0 && Near(p.y, 40.0),
		"t=0 : bateau 0 a quai en A, 30 s avant le depart");
	p = f.Evaluate(0, 29.0);
	Check(p.docked && p.departures == 0, "t=29 : toujours a quai");
	p = f.Evaluate(0, 80.0); // 50 s de route : 500 m
	Check(!p.docked && p.fromStop == 0 && p.stop == 1 && Near(p.x, 500.0) && Near(p.z, 0.0) && Near(p.speed, 10.0)
		&& p.departures == 1 && Near(p.yawRad, kPi / 2.0, 1e-4) && Near(p.secondsLeft, 50.0),
		"t=80 : en mer vers B, a mi-chemin, cap est, 1 depart");
	p = f.Evaluate(0, 135.0); // arrivée en B à t=130, à quai jusqu'à 160
	Check(p.docked && p.stop == 1 && Near(p.x, 1000.0) && Near(p.secondsLeft, 25.0) && p.departures == 1
		&& Near(p.yawRad, 80.0 * kPi / 180.0, 1e-4), "t=135 : a quai en B, cap du quai (80 deg, pas 260) le plus proche de l'arrivee (est)");
	p = f.Evaluate(0, 160.0 + 50.0); // 500 m vers C (nord… +Z)
	Check(!p.docked && p.stop == 2 && Near(p.x, 1000.0) && Near(p.z, 500.0) && Near(p.yawRad, 0.0, 1e-4) && p.departures == 2,
		"en route vers C, cap +Z");
	p = f.Evaluate(0, period + 10.0);
	Check(p.docked && p.stop == 0 && p.departures == 3, "un cycle plus tard : de nouveau a quai en A (3 departs)");
	const BoatPose q = f.Evaluate(1, 0.0);
	const BoatPose q2 = f.Evaluate(0, period / 2.0);
	Check(Near(q.x, q2.x) && Near(q.z, q2.z) && q.docked == q2.docked, "bateau 1 : un demi-cycle d'avance");
	// Virage au sommet (500, 0) d'un segment droit : pas de saut de cap.
	p = f.Evaluate(0, 30.0 + 49.0);
	Check(Near(p.yawRad, kPi / 2.0, 1e-4), "sommet aligne : cap inchange");
	// Sortie de quai A : le cap part de celui d'arrivée (venue de C au sud-ouest : quai 0 ou
	// 180 deg, le plus proche de -135 deg est 180) puis tourne vers l'est.
	p = f.Evaluate(0, 30.01);
	Check(!p.docked && std::fabs(p.yawRad) > kPi - 0.05, "sortie de quai : cap d'arrivee au quai (180 deg)");
	p = f.Evaluate(0, 30.0 + 4.1);
	Check(Near(p.yawRad, kPi / 2.0, 1e-3), "sortie de quai : cap de route apres le virage (40 m)");

	// Aller-retour D → E → F → E → D (4 étapes : 1000, 2000, 2000, 1000 m, 20 s d'arrêt).
	Check(Near(f.CycleSeconds(2), 4 * 20.0 + 6000.0 / 10.0, 1e-6), "aller-retour : periode");
	p = f.Evaluate(2, 20.0 + 100.0 + 20.0 + 200.0 + 20.0 + 100.0); // en route F → E, à mi-chemin
	Check(!p.docked && p.fromStop == 2 && p.stop == 1 && Near(p.z, 7000.0) && Near(std::fabs(p.yawRad), kPi, 1e-4),
		"aller-retour : retour F -> E, cap -Z (etape lue a l'envers)");

	// Repère du bateau.
	p = BoatPose{};
	p.x = 100.0; p.z = 200.0; p.yawRad = static_cast<float>(kPi / 2.0);
	double wx = 0, wz = 0, lx = 0, lz = 0;
	BoatFleet::LocalToWorld(p, 0.0, 10.0, wx, wz);
	Check(Near(wx, 110.0) && Near(wz, 200.0), "repere : proue (+Z local) vers +X monde au cap pi/2");
	BoatFleet::WorldToLocal(p, wx, wz, lx, lz);
	Check(Near(lx, 0.0) && Near(lz, 10.0), "repere : aller-retour monde/local");
	float dx = 5.0f, dz = -20.0f;
	Check(BoatFleet::ClampToDeck(dx, dz) && dx == BoatFleet::kDeckHalfX && dz == BoatFleet::kDeckMinZ, "pont : point ramene");
	dx = 1.0f; dz = 2.0f;
	Check(!BoatFleet::ClampToDeck(dx, dz) && dx == 1.0f, "pont : point sur le pont inchange");
	dx = std::nanf(""); dz = 0.0f;
	Check(BoatFleet::ClampToDeck(dx, dz) && dx == 0.0f, "pont : NaN ramene au centre");

	if (g_fail == 0) std::printf("sea_lines_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
