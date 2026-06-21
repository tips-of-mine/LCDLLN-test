#include "src/client/world/BuildingCollisionCatalog.h"

#include <cmath>
#include <cstdio>

using engine::world::BuildingCollisionCatalog;

namespace { int g_fail = 0; void check(bool c, const char* m){ if(!c){ std::printf("FAIL: %s\n", m); ++g_fail; } } }

int main()
{
	const char* json = R"({
		"pieces": {
			"door_1_flat": { "passable": true },
			"wall_plaster_straight": {
				"box_count": 1,
				"box_0": { "cx": 0.0, "cy": 1.5, "cz": 0.0, "hx": 1.0, "hy": 1.5, "hz": 0.1 }
			},
			"wall_plaster_door_flat": {
				"box_count": 2,
				"box_0": { "cx": -0.8, "cy": 1.5, "cz": 0.0, "hx": 0.2, "hy": 1.5, "hz": 0.1 },
				"box_1": { "cx":  0.8, "cy": 1.5, "cz": 0.0, "hx": 0.2, "hy": 1.5, "hz": 0.1 }
			}
		}
	})";

	BuildingCollisionCatalog cat;
	check(cat.LoadFromJson(json), "load: ok");

	// 1) passable
	{
		const auto* e = cat.Lookup("Door_1_Flat"); // casse d'origine -> normalisée
		check(e != nullptr, "door: present");
		check(e && e->passable, "door: passable");
		check(e && e->boxes.empty(), "door: pas de boites");
	}
	// 2) une boîte
	{
		const auto* e = cat.Lookup("wall_plaster_straight");
		check(e != nullptr && !e->passable, "wall: present non passable");
		check(e && e->boxes.size() == 1, "wall: 1 boite");
		check(e && std::fabs(e->boxes[0].hx - 1.0f) < 1e-4f, "wall: hx=1.0");
		check(e && std::fabs(e->boxes[0].cy - 1.5f) < 1e-4f, "wall: cy=1.5");
	}
	// 3) multi-boîtes
	{
		const auto* e = cat.Lookup("Wall_Plaster_Door_Flat");
		check(e != nullptr && e->boxes.size() == 2, "doorwall: 2 boites");
	}
	// 4) mesh absent -> nullptr (fallback cylindre)
	check(cat.Lookup("Tree_Oak") == nullptr, "absent: nullptr");

	// 5) JSON invalide -> LoadFromJson renvoie false
	{
		BuildingCollisionCatalog bad;
		check(!bad.LoadFromJson("{ ceci n'est pas du json"), "invalide: load echoue");
	}

	if (g_fail == 0) std::printf("BuildingCollisionCatalogTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
