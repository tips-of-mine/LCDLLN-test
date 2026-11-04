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

	// Seconde carte : fusion de la table des Enfers (départ des démons).
	namespace cells = engine::world::cells;
	const char* royaumes = R"({ "schema_version": 1, "world": "royaumes", "generated_cells": "N003_W009,N000_E000",
	  "races": { "humains": { "city": "hameau_marn", "x": -90000, "y": 60, "z": -30000 } } })";
	const char* enfers = R"({ "schema_version": 1, "world": "enfers", "generated_cells": "N000_E000,N001_E000,S001_E000,N000_E001,N000_W001",
	  "races": { "demons": { "city": "citadelle_legion", "city_name": "Citadelle de la Legion", "x": 10, "y": 200, "z": -20 } } })";
	Check(t.LoadFromText(royaumes, err) && !t.HasMap(cells::CellMap::Enfers), "royaumes seuls");
	Check(!t.IsGeneratedCell({ 0, 0 }, cells::CellMap::Enfers), "enfers non charges : aucune cellule jouable");
	Check(t.MergeFromText(enfers, err), "fusion des enfers");
	Check(t.Count() == 2 && t.HasMap(cells::CellMap::Enfers) && t.GeneratedCells(cells::CellMap::Enfers).size() == 5
		&& t.GeneratedCells().size() == 2, "cellules par carte");
	const auto* demon = t.Resolve("demons");
	Check(demon && demon->map == cells::CellMap::Enfers && demon->cityId == "citadelle_legion", "demons : depart aux enfers");
	Check(t.Resolve("humains") && t.Resolve("humains")->map == cells::CellMap::Royaumes, "humains : depart aux royaumes");
	Check(t.IsGeneratedCell({ 1, 0 }, cells::CellMap::Enfers) && !t.IsGeneratedCell({ 1, 0 })
		&& t.IsGeneratedCell({ 0, 0 }) && !t.IsGeneratedCell({ 1, 1 }, cells::CellMap::Enfers), "appartenance par carte");
	Check(!t.IsGeneratedCell({ 2, 0 }, cells::CellMap::Enfers), "hors grille des enfers");
	Check(!t.MergeFromText(enfers, err) && t.Count() == 2, "carte deja chargee : fusion refusee");
	Check(t.Resolve("orcs") && t.Resolve("orcs")->race == "humains", "race par defaut de la table principale");
	if (g_fail == 0) std::printf("StartLocationsTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
