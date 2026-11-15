// sea_lines_tests — v1.0.124 : lignes maritimes (passeur au ponton).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/SeaLines.h"

#include <cstdio>
#include <string>

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}

	const char* kJson = R"({
  "schema_version": 1, "world": "royaumes",
  "lines": [
    { "id": "boucle", "name": "Boucle", "loop": true, "price_gold_per_leg": 10, "stops": [
      { "city": "a", "name": "A", "cell": "N000_E000", "x": 0, "y": 41, "z": 0, "yaw_deg": 90 },
      { "city": "b", "name": "B", "cell": "N000_E001", "x": 1000, "y": 42, "z": 0 },
      { "city": "c", "name": "C", "cell": "N000_E002", "x": 2000, "y": 43, "z": 0 },
      { "city": "d", "name": "D", "cell": "N000_E003", "x": 3000, "y": 44, "z": 0 } ] },
    { "id": "navette", "name": "Navette", "loop": false, "price_gold_per_leg": 5, "stops": [
      { "city": "e", "cell": "N001_E000", "x": 0, "y": 40, "z": 5000 },
      { "city": "f", "cell": "N002_E000", "x": 0, "y": 40, "z": 9000 } ] },
    { "id": "sans_ponton", "loop": false, "price_gold_per_leg": 5, "stops": [
      { "city": "g", "cell": "N001_E000", "x": 0, "z": 0 },
      { "city": "h", "cell": "N002_E000" } ] }
  ]
})";
}

int main()
{
	using namespace engine::server;
	using engine::world::cells::CellMap;
	SeaLines s;
	std::string err;
	Check(!s.AddFromText("{", err), "JSON invalide refuse");
	Check(s.AddFromText(kJson, err) && s.Count() == 2, "2 lignes chargees (escale sans ponton ignoree)");
	const SeaLine* b = s.Find("boucle");
	Check(b && b->stops.size() == 4 && b->loop && b->stops[0].yawDeg == 90.0f, "boucle : 4 escales");
	Check(b && SeaLines::Legs(*b, 0, 3) == 1u && SeaLines::Legs(*b, 0, 2) == 2u && SeaLines::Legs(*b, 1, 3) == 2u,
		"boucle : plus court sens");
	const SeaLine* n = s.Find("navette");
	Check(n && SeaLines::Legs(*n, 1, 0) == 1u, "navette : 1 etape");

	SeaTravelPlan p;
	Check(s.Plan("inconnue", 1, CellMap::Royaumes, 0, 0, p) == SeaTravelRefusal::UnknownLine, "ligne inconnue");
	Check(s.Plan("boucle", 1, CellMap::Enfers, 0, 0, p) == SeaTravelRefusal::UnknownLine, "autre carte : refus");
	Check(s.Plan("boucle", 9, CellMap::Royaumes, 0, 0, p) == SeaTravelRefusal::BadStop, "escale inexistante");
	Check(s.Plan("boucle", 1, CellMap::Royaumes, 500, 0, p) == SeaTravelRefusal::NotAtPonton, "loin du ponton");
	Check(s.Plan("boucle", 0, CellMap::Royaumes, 10, 10, p) == SeaTravelRefusal::SameStop, "deja a l'escale");
	Check(s.Plan("boucle", 3, CellMap::Royaumes, 20, 5, p) == SeaTravelRefusal::None && p.fromStop == 0 && p.legs == 1u
		&& p.priceGold == 10u && p.arrival && p.arrival->x == 3000.0 && p.arrival->y == 44.0, "A -> D : 1 etape, 10 or");
	Check(s.Plan("boucle", 2, CellMap::Royaumes, 1010, 0, p) == SeaTravelRefusal::None && p.priceGold == 10u, "B -> C : 10 or");
	Check(s.Plan("navette", 1, CellMap::Royaumes, 0, 5020, p) == SeaTravelRefusal::None && p.priceGold == 5u
		&& p.arrival->z == 9000.0, "navette : 5 or");

	if (g_fail == 0) std::printf("sea_lines_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
