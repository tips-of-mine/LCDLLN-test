// portal_network_tests — portails entre cartes (Royaumes ↔ Enfers), module pur.
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/PortalNetwork.h"

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

	/// Portails du continent : les 5 Portes des enfers, appariées I..V.
	const char* kRoyaumes = R"({ "schema_version": 1, "world": "royaumes", "portal_count": 6, "portals": [
	  { "id": "poi_porte_1", "link": "enfers:poi_enfers_porte_1", "x": 0, "y": 60, "z": 130000, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "poi_porte_2", "link": "enfers:poi_enfers_porte_2", "x": 70000, "y": 70, "z": -80000, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "poi_porte_3", "link": "enfers:poi_enfers_porte_3", "x": -110000, "y": 70, "z": 50000, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "poi_porte_4", "link": "enfers:poi_enfers_porte_4", "x": 110000, "y": 70, "z": 50000, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "poi_porte_5", "link": "enfers:poi_enfers_porte_5", "x": -70000, "y": 70, "z": -80000, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "decor", "link": "", "x": 5, "y": 0, "z": 5 } ] })";
	/// Portails des Enfers (repère propre : mêmes coordonnées que le continent possibles).
	const char* kEnfers = R"({ "schema_version": 1, "world": "enfers", "portal_count": 5, "portals": [
	  { "id": "poi_enfers_porte_1", "link": "royaumes:poi_porte_1", "x": 0, "y": 270, "z": 10000, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "poi_enfers_porte_2", "link": "royaumes:poi_porte_2", "x": 0, "y": 280, "z": -10000, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "poi_enfers_porte_3", "link": "royaumes:poi_porte_3", "x": -10000, "y": 280, "z": 0, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "poi_enfers_porte_4", "link": "royaumes:poi_porte_4", "x": 10000, "y": 280, "z": 0, "radius_m": 6, "arrival_offset_m": 8 },
	  { "id": "poi_enfers_porte_5", "link": "royaumes:poi_porte_5", "x": 30, "y": 310, "z": 300, "radius_m": 6, "arrival_offset_m": 8 } ] })";
}

int main()
{
	using engine::server::PortalDestination;
	using engine::server::PortalNetwork;
	using engine::server::PortalTravelState;
	using engine::world::cells::CellMap;

	PortalNetwork net;
	std::string err;
	Check(net.AddFromText(kRoyaumes, err) && net.AddFromText(kEnfers, err), "chargement des deux cartes");
	Check(net.Count() == 10 && net.LinkedCount() == 10, "10 portails relies (portail sans lien ignore)");
	Check(!net.AddFromText(R"({ "schema_version": 1, "world": "feyhin", "portals": [] })", err), "monde inconnu refuse");
	Check(!net.AddFromText("{ pas du json", err) && net.Count() == 10, "JSON invalide : rien ajoute");

	// Appariement I..V dans les deux sens.
	bool pairsOk = true;
	for (int k = 1; k <= 5; ++k)
	{
		const std::string gate = "poi_porte_" + std::to_string(k);
		const std::string hell = "poi_enfers_porte_" + std::to_string(k);
		const auto* g = net.Find(CellMap::Royaumes, gate);
		const auto* h = net.Find(CellMap::Enfers, hell);
		const auto down = g ? net.DestinationOf(*g) : std::nullopt;
		const auto up = h ? net.DestinationOf(*h) : std::nullopt;
		pairsOk = pairsOk && down && up && down->map == CellMap::Enfers && down->toId == hell
			&& up->map == CellMap::Royaumes && up->toId == gate;
	}
	Check(pairsOk, "appariement I..V dans les deux sens");

	// Rayon : 6 m ; arrivée à 8 m du portail apparié (hors de son rayon), même carte que lui.
	Check(net.PortalAt(CellMap::Royaumes, 0.0, 130005.9) != nullptr && net.PortalAt(CellMap::Royaumes, 0.0, 130006.5) == nullptr,
		"rayon de declenchement 6 m");
	Check(net.PortalAt(CellMap::Enfers, 0.0, 130000.0) == nullptr, "un portail n'existe que sur sa carte");
	const auto arrival = net.DestinationOf(*net.Find(CellMap::Royaumes, "poi_porte_1"));
	Check(arrival && std::fabs(arrival->x - 0.0) < 1e-9 && std::fabs(arrival->z - 10008.0) < 1e-9 && std::fabs(arrival->y - 270.0) < 1e-9,
		"arrivee decalee de 8 m");
	Check(arrival && net.PortalAt(CellMap::Enfers, arrival->x, arrival->z) == nullptr, "arrivee hors du rayon d'arrivee");

	// Joueur (toute race : la race n'intervient pas) : armement, passage, réarmement.
	PortalTravelState st;
	Check(!st.armed, "desarme a la connexion");
	Check(!net.Update(st, CellMap::Royaumes, 0.0, 130000.0, true, 1000).has_value(), "connexion dans un portail : pas de passage");
	Check(!net.Update(st, CellMap::Royaumes, 0.0, 130020.0, true, 2000).has_value() && !st.armed, "sortie du rayon : chrono demarre");
	Check(!net.Update(st, CellMap::Royaumes, 0.0, 130020.0, true, 6999).has_value() && !st.armed, "4,999 s hors rayon : toujours desarme");
	Check(!net.Update(st, CellMap::Royaumes, 0.0, 130020.0, true, 7000).has_value() && st.armed, "5 s hors rayon : rearme");
	// Mort : jamais de passage, l'armement est conservé.
	Check(!net.Update(st, CellMap::Royaumes, 0.0, 130001.0, false, 8000).has_value() && st.armed, "joueur mort : pas de passage");
	const std::optional<PortalDestination> go = net.Update(st, CellMap::Royaumes, 0.0, 130001.0, true, 9000);
	Check(go && go->map == CellMap::Enfers && go->fromId == "poi_porte_1" && go->toId == "poi_enfers_porte_1" && !st.armed,
		"vivant et arme dans le rayon : passage vers les enfers, desarme");
	// Retour immédiat impossible : il faut sortir 5 s de tout rayon.
	Check(!net.Update(st, CellMap::Enfers, go->x, go->z, true, 9100).has_value(), "arrivee : pas de renvoi");
	Check(!net.Update(st, CellMap::Enfers, 0.0, 10001.0, true, 9500).has_value() && !st.armed, "retour dans le rayon avant 5 s : refuse");
	Check(!net.Update(st, CellMap::Enfers, 0.0, 10020.0, true, 10000).has_value(), "ressorti : chrono repart");
	Check(!net.Update(st, CellMap::Enfers, 0.0, 10020.0, true, 15000).has_value() && st.armed, "rearme 5 s apres la sortie");
	const std::optional<PortalDestination> back = net.Update(st, CellMap::Enfers, 0.0, 10002.0, true, 15500);
	Check(back && back->map == CellMap::Royaumes && back->toId == "poi_porte_1", "retour vers le continent");
	// N'importe quelle porte : la porte V des Enfers mène à la Porte V du continent.
	PortalTravelState other;
	other.armed = true;
	const auto viaV = net.Update(other, CellMap::Enfers, 30.0, 302.0, true, 0);
	Check(viaV && viaV->toId == "poi_porte_5" && std::fabs(viaV->z - (-80000.0 + 8.0)) < 1e-9, "porte V vers la Porte V");
	Check(!net.Update(other, CellMap::Royaumes, NAN, 0.0, true, 1).has_value(), "position invalide ignoree");

	if (g_fail == 0) std::printf("portal_network_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
