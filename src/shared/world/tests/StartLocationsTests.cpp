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

	// Cellules générées (monde en cellules) : liste optionnelle, validée.
	Check(t.LoadFromText(json, err) && t.GeneratedCells().empty() && t.IsGeneratedCell({ 0, 0 }),
		"sans liste : aucune restriction");
	const char* withCells = R"({ "schema_version": 1, "generated_cells": "N003_W009, S009_E011",
	  "races": { "humains": { "x": -90000, "y": 60, "z": -30000 } } })";
	Check(t.LoadFromText(withCells, err) && t.GeneratedCells().size() == 2, "deux cellules generees");
	Check(t.IsGeneratedCell({ 3, -9 }) && t.IsGeneratedCell({ -9, 11 }) && !t.IsGeneratedCell({ 0, 0 }),
		"appartenance aux cellules generees");
	Check(!t.IsGeneratedCell({ 600, 0 }), "hors carte du lore : jamais generee");
	Check(t.All().size() == 1 && t.All()[0].race == "humains", "liste des departs");
	Check(!t.LoadFromText(R"({ "schema_version": 1, "generated_cells": "N003_X009",
	  "races": { "humains": { "x": 0, "z": 0 } } })", err) && t.Count() == 0, "cellule invalide rejetee");
	if (g_fail == 0) std::printf("StartLocationsTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
