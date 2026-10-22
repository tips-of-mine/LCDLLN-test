// start_locations_tests — table des points de départ par race (master).
#include "src/shared/world/StartLocations.h"

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
}

int main()
{
	using engine::world::StartLocationTable;
	const char* json = R"({ "schema_version": 1, "world": "royaumes", "default_race": "humains",
	  "races": {
	    "humains": { "city": "hameau_marn", "city_name": "Hameau de Marn", "x": 0, "y": 52.5, "z": 0, "yaw_deg": 0 },
	    "elfes": { "city": "ville_whispers_hill", "city_name": "Whispers Hill", "x": 2176.5, "y": 61, "z": -1025.25, "yaw_deg": 90 } } })";
	StartLocationTable t;
	std::string err;
	Check(t.LoadFromText(json, err), "chargement valide");
	Check(t.Count() == 2, "deux races");
	const auto* e = t.Resolve("elfes");
	Check(e && e->cityId == "ville_whispers_hill" && std::fabs(e->x - 2176.5) < 1e-6 && std::fabs(e->z + 1025.25) < 1e-6, "elfes : Whispers Hill");
	const auto* unknown = t.Resolve("orcs");
	Check(unknown && unknown->race == "humains", "race absente -> race par defaut");
	Check(!t.LoadFromText(R"({ "schema_version": 2, "races": {} })", err), "schema_version inconnu rejete");
	Check(t.Resolve("humains") == nullptr, "table videe apres echec");
	Check(!t.LoadFromText("{ pas du json", err), "JSON invalide rejete");
	if (g_fail == 0) std::printf("StartLocationsTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
