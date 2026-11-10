// start_locations_tests — table des points de départ par faction, repli par race (master).
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
	const char* enfers = R"({ "schema_version": 1, "world": "enfers", "generated_cells": "N000_E000,N001_E000,S001_E000,N000_E001,N000_W001,N001_W001,N001_E001,S001_W001,S001_E001",
	  "races": { "demons": { "city": "citadelle_legion", "city_name": "Citadelle de la Legion", "x": 10, "y": 200, "z": -20 } } })";
	Check(t.LoadFromText(royaumes, err) && !t.HasMap(cells::CellMap::Enfers), "royaumes seuls");
	Check(!t.IsGeneratedCell({ 0, 0 }, cells::CellMap::Enfers), "enfers non charges : aucune cellule jouable");
	Check(t.MergeFromText(enfers, err), "fusion des enfers");
	Check(t.Count() == 2 && t.HasMap(cells::CellMap::Enfers) && t.GeneratedCells(cells::CellMap::Enfers).size() == 9
		&& t.GeneratedCells().size() == 2, "cellules par carte");
	const auto* demon = t.Resolve("demons");
	Check(demon && demon->map == cells::CellMap::Enfers && demon->cityId == "citadelle_legion", "demons : depart aux enfers");
	Check(t.Resolve("humains") && t.Resolve("humains")->map == cells::CellMap::Royaumes, "humains : depart aux royaumes");
	Check(t.IsGeneratedCell({ 1, 0 }, cells::CellMap::Enfers) && !t.IsGeneratedCell({ 1, 0 })
		&& t.IsGeneratedCell({ 0, 0 }) && t.IsGeneratedCell({ 1, 1 }, cells::CellMap::Enfers) && !t.IsGeneratedCell({ 1, 1 }),
		"appartenance par carte (coin des enfers genere depuis la v1.0.71)");
	Check(!t.IsGeneratedCell({ 2, 0 }, cells::CellMap::Enfers), "hors grille des enfers");
	Check(!t.MergeFromText(enfers, err) && t.Count() == 2, "carte deja chargee : fusion refusee");
	Check(t.Resolve("orcs") && t.Resolve("orcs")->race == "humains", "race par defaut de la table principale");

	// v1.0.61 : départs par faction (prioritaires), repli par race.
	const char* byFaction = R"({ "schema_version": 1, "world": "royaumes", "default_race": "humains",
	  "generated_cells": "N002_W010,N005_W011", "start_factions": "empire_hynn, lumiere,fantome",
	  "factions": {
	    "lumiere": { "city": "hameau_lumiere", "city_name": "Lumiere", "race": "humains", "x": -110000, "y": 60, "z": -50000, "yaw_deg": 45 },
	    "empire_hynn": { "city": "hameau_legion", "race": "humains", "x": -100000, "y": 55, "z": -20000 },
	    "nains": { "city": "hors_liste", "x": 1, "z": 1 } },
	  "races": { "humains": { "city": "hameau_legion", "x": -100000, "y": 55, "z": -20000 } } })";
	Check(t.LoadFromText(byFaction, err) && t.Count() == 3 && t.FactionCount() == 2, "factions listees seulement (absente ou hors liste ignoree)");
	const auto* lum = t.ResolveFor("lumiere", "humains");
	Check(lum && lum->faction == "lumiere" && lum->race == "humains" && lum->cityId == "hameau_lumiere"
		&& std::fabs(lum->yawDeg - 45.0) < 1e-9, "faction lumiere : son hameau");
	const auto* noFaction = t.ResolveFor("", "humains");
	Check(noFaction && noFaction->faction.empty() && noFaction->race == "humains", "sans faction : repli race");
	const auto* unknownFaction = t.ResolveFor("serpent", "orcs");
	Check(unknownFaction && unknownFaction->race == "humains", "faction sans depart : repli race puis race par defaut");
	Check(t.Resolve("humains") && t.Resolve("humains")->faction.empty(), "Resolve : race seulement");
	const char* hellFaction = R"({ "schema_version": 1, "world": "enfers", "generated_cells": "N000_E000",
	  "start_factions": "legion", "factions": { "legion": { "city": "citadelle_legion", "race": "demons", "x": 0, "y": 200, "z": 0 } },
	  "races": { "demons": { "city": "citadelle_legion", "x": 0, "y": 200, "z": 0 } } })";
	Check(t.MergeFromText(hellFaction, err) && t.FactionCount() == 3, "fusion : faction des enfers");
	const auto* legion = t.ResolveFor("legion", "demons");
	Check(legion && legion->map == cells::CellMap::Enfers && legion->faction == "legion", "legion : depart aux enfers");
	const char* dupFaction = R"({ "schema_version": 1, "world": "enfers", "start_factions": "lumiere",
	  "factions": { "lumiere": { "x": 0, "z": 0 } } })";
	StartLocationTable t2;
	Check(t2.LoadFromText(byFaction, err) && !t2.MergeFromText(dupFaction, err) && t2.FactionCount() == 2, "faction deja dotee : fusion refusee");
	Check(t2.LoadFromText(R"({ "schema_version": 1, "start_factions": "dzorak",
	  "factions": { "dzorak": { "city": "hameau_dzorak", "race": "orcs", "x": 5, "z": 6 } } })", err)
		&& t2.ResolveFor("dzorak", "orcs") && t2.ResolveFor("lumiere", "humains") == nullptr, "factions sans races : valide, aucun repli");
	Check(!t2.LoadFromText(R"({ "schema_version": 1, "races": {} })", err), "ni race ni faction : rejete");
	if (g_fail == 0) std::printf("StartLocationsTests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
