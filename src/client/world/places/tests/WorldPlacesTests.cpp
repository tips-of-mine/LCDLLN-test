// world_places_tests — lieux du monde (carte M + bannière d'entrée).
#include "src/client/world/places/WorldPlaces.h"

#include "src/shared/core/Config.h"

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
	using namespace engine::client;
	const char* json = R"({ "schema_version": 1, "world": "t", "display_name": "Test", "map_image": "world/t/world_map.png",
	  "terrain": { "origin_x": -100, "origin_z": -200, "size_m": 400 },
	  "city_count": 3, "cities": [
	    { "id": "a", "name": "Alpha", "kind": "capitale", "race": "humains", "state": "intact", "x": 0, "z": 0, "radius_m": 30, "start_for": ["lumiere"], "description": "d" },
	    { "id": "b", "name": "Beta", "kind": "village", "race": "elfes", "state": "ruine", "x": 120, "z": 50, "radius_m": 10, "start_for": [], "description": "" },
	    { "id": "g", "name": "Gamma", "kind": "ville", "race": "humains", "state": "reconstruction", "x": 185, "z": 90, "radius_m": 6, "start_for": [], "description": "" } ],
	  "poi_count": 1, "pois": [ { "id": "p", "name": "Oracle", "kind": "monolithe", "x": 10, "z": 10, "description": "" } ],
	  "region_count": 2, "regions": [
	    { "id": "big", "name": "Grande", "x_min": -100, "x_max": 300, "z_min": -200, "z_max": 200 },
	    { "id": "small", "name": "Petite", "x_min": 100, "x_max": 200, "z_min": 0, "z_max": 100 } ],
	  "cell_count": 2, "cells": [ { "name": "cell_n003_w009", "lore": "N003_W009" }, { "name": "cell_s009_e011", "lore": "S009_E011" } ] })";
	WorldPlaces w;
	std::string err;
	Check(w.LoadFromText(json, err), "chargement valide");
	Check(w.Cities().size() == 3 && w.Pois().size() == 1 && w.Regions().size() == 2, "comptes");
	Check(w.GeneratedCells().size() == 2 && w.GeneratedCells()[1] == "S009_E011", "cellules generees");
	Check(w.Cities()[0].startFor.size() == 1 && w.Cities()[0].startFor[0] == "lumiere", "start_for (faction)");
	Check(w.StartCityFor("lumiere") == &w.Cities()[0], "ville de depart de la faction lumiere");
	Check(w.StartCityFor("dzorak") == nullptr && w.StartCityFor("humains") == nullptr, "pas de depart dzorak ni par race");

	float u = 0, v = 0;
	Check(w.WorldToMapUv(-100, -200, u, v) && std::fabs(u) < 1e-6 && std::fabs(v) < 1e-6, "uv origine");
	Check(w.WorldToMapUv(100, 0, u, v) && std::fabs(u - 0.5f) < 1e-6 && std::fabs(v - 0.5f) < 1e-6, "uv centre");
	Check(!w.WorldToMapUv(400, 0, u, v), "hors carte");
	float x = 0, z = 0;
	w.MapUvToWorld(0.25f, 0.75f, x, z);
	Check(std::fabs(x - 0.0f) < 1e-4 && std::fabs(z - 100.0f) < 1e-4, "uv -> monde");

	Check(w.CityAt(5, 5) && w.CityAt(5, 5)->id == "a", "dans Alpha");
	Check(w.CityAt(60, 0) == nullptr, "hors ville");
	Check(w.CityAt(33, 0, 6.0f) != nullptr, "marge");
	Check(w.RegionAt(150, 50) && w.RegionAt(150, 50)->id == "small", "region la plus specifique");
	Check(w.RegionAt(0, 0) && w.RegionAt(0, 0)->id == "big", "grande region");
	Check(w.RegionAt(1000, 0) == nullptr, "aucune region");
	Check(engine::client::PlaceKindLabel("volcan") == "Volcan" && engine::client::PlaceKindLabel("gouffre") == "Gouffre",
		"PlaceKindLabel : genres de POI volcan et gouffre (v1.0.52)");
	Check(engine::client::PlaceKindLabel("temple") == "Temple" && engine::client::PlaceKindLabel("cite") == "Cit\xC3\xA9"
		&& engine::client::PlaceKindLabel("defile") == "D\xC3\xA9" "fil\xC3\xA9" && engine::client::PlaceKindLabel("salle") == "Salle souterraine",
		"PlaceKindLabel : genres de POI des tomes 7 a 14 (v1.0.55)");
	Check(engine::client::PlaceKindLabel("elevage") == "\xC3\x89levage", "PlaceKindLabel : genre de POI des albums hors serie (v1.0.59)");
	Check(engine::client::PlaceKindLabel("champ_bataille") == "Champ de bataille" && engine::client::PlaceKindLabel("etang") == "\xC3\x89tang"
	      && engine::client::PlaceKindLabel("inconnu") == "inconnu", "libelles des genres de POI");

	// Bannière : accueil dans la ville de départ, puis entrée de région/ville.
	PlaceBannerTracker t;
	t.Update(w, 0, 0, 0.016f);
	Check(t.Visible() && t.Title() == "Alpha", "accueil ville");
	for (int i = 0; i < 400; ++i) t.Update(w, 0, 0, 0.016f);
	Check(!t.Visible(), "banniere expiree");
	t.Update(w, 34, 0, 0.016f); // rayon 30 + hysteresis 14 : toujours dans Alpha
	Check(!t.Visible() && t.CurrentCityId() == "a", "hysteresis de sortie");
	t.Update(w, 60, -100, 0.016f); // sorti de la ville, même région
	Check(!t.Visible() && t.CurrentCityId().empty(), "sortie silencieuse");
	t.Update(w, 150, 80, 0.016f); // entre dans la petite région
	Check(t.Visible() && t.Title() == "Petite", "entree de region");
	t.Update(w, 120, 50, 0.016f); // entre dans Beta (ruine)
	Check(t.Visible() && t.Title() == "Beta" && t.Subtitle().find("ruine") != std::string::npos, "entree de ville en ruine");
	t.Update(w, 185, 90, 0.016f); // entre dans Gamma (reconstruction), même région
	Check(t.Visible() && t.Title() == "Gamma" && t.Subtitle().find("en reconstruction") != std::string::npos,
		"entree de ville en reconstruction");
	Check(t.Alpha() >= 0.0f && t.Alpha() <= 1.0f, "alpha borne");

	Check(!w.LoadFromText("{ \"schema_version\": 2 }", err) && !w.IsLoaded(), "schema refuse");

	// Données générées réelles (si présentes) : chaque race jouable a sa ville.
	engine::core::Config cfg;
	cfg.SetValue("paths.content", engine::core::Config::Value{ std::string("game/data") });
	WorldPlaces real;
	if (real.LoadFromContent(cfg, "royaumes", err))
	{
		Check(real.Cities().size() >= 30, "royaumes : villes");
		// v1.0.61 : un départ par faction de jeu ; la Légion infernale naît aux Enfers.
		for (const char* faction : { "lumiere", "justice", "dzorak", "dragons", "naine", "elfe", "legions_imperiales" })
			Check(real.StartCityFor(faction) != nullptr && real.StartCityFor(faction)->kind == "hameau", faction);
		Check(real.StartCityFor("legion") == nullptr, "royaumes : pas de depart de la Legion sur le continent");
		Check(real.StartCityFor("humains") == nullptr, "royaumes : plus de depart par race");
		// Repère global des cellules : Marn au centre de la cellule N003_W009.
		const PlaceCity* marn = real.CityAt(-90000.0f, -30000.0f);
		Check(marn && marn->id == "hameau_marn", "royaumes : hameau de Marn au centre de N003_W009");
		Check(real.WorldToMapUv(0, 0, u, v), "royaumes : origine sur la carte");
	}
	else
	{
		std::printf("note: %s (test donnees reelles ignore)\n", err.c_str());
	}

	// Seconde carte (si générée) : la Citadelle de la Légion, départ des démons.
	WorldPlaces hell;
	if (hell.LoadFromContent(cfg, "enfers", err))
	{
		const PlaceCity* citadel = hell.StartCityFor("legion");
		Check(citadel && citadel->id == "citadelle_legion", "enfers : depart de la Legion infernale a la Citadelle");
		Check(hell.GeneratedCells().size() == 5u, "enfers : 5 cellules generees");
		Check(hell.StartCityFor("lumiere") == nullptr, "enfers : aucun depart du continent");
	}
	else
	{
		std::printf("note: %s (test donnees enfers ignore)\n", err.c_str());
	}

	if (g_fail == 0) std::printf("world_places_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
