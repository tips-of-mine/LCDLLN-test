// door_passages_tests — passages conditionnels d'une même carte (Camp de l'Oracle,
// lore v1.0.141), module pur. Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/DoorPassages.h"

#include <cmath>
#include <cstdio>
#include <set>
#include <string>

namespace
{
	int g_fail = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::printf("FAIL: %s\n", what); ++g_fail; }
	}

	/// Grande porte SO (quête), porte E (quête + raid de 3), sorties sans condition,
	/// entrée invalide (arrivée dans son propre rayon) et entrée sans identifiant.
	const char* kRoyaumes = R"({ "schema_version": 1, "world": "royaumes", "passage_count": 6, "passages": [
	  { "id": "oracle_so_entree", "name": "Porte SO", "x": -100, "y": 22, "z": 100, "radius_m": 2.5,
	    "to_x": -30, "to_y": 22.2, "to_z": 30, "requires_quests": ["oracle_appel"], "min_group": 0,
	    "denied_message": "La porte reste close." },
	  { "id": "oracle_so_sortie", "x": -32, "y": 22, "z": 32, "radius_m": 2.0, "to_x": -97, "to_y": 22.2, "to_z": 97 },
	  { "id": "oracle_e_entree", "x": 140, "y": 22, "z": 0, "radius_m": 2.5, "to_x": 110, "to_y": 22.2, "to_z": 0,
	    "requires_quests": ["oracle_appel", "oracle_echos"], "min_group": 3, "denied_message": "Il faut un raid." },
	  { "id": "oracle_e_sortie", "x": 114, "y": 22, "z": 0, "radius_m": 2.0, "to_x": 137, "to_y": 22.2, "to_z": 0 },
	  { "id": "boucle", "x": 0, "y": 0, "z": 0, "radius_m": 3.0, "to_x": 1, "to_y": 0, "to_z": 1 },
	  { "name": "sans id", "x": 5, "y": 0, "z": 5, "to_x": 50, "to_z": 50 } ] })";
}

int main()
{
	using engine::server::DoorPassage;
	using engine::server::DoorPassageOutcome;
	using engine::server::DoorPassages;
	using engine::server::DoorPassageTravelState;
	using engine::world::cells::CellMap;

	DoorPassages net;
	std::string err;
	Check(net.AddFromText(kRoyaumes, err), "chargement");
	Check(net.Count() == 4, "4 passages (boucle et entree sans id ignorees)");
	Check(!net.AddFromText(R"({ "schema_version": 2, "world": "royaumes", "passages": [] })", err), "schema_version refuse");
	Check(!net.AddFromText(R"({ "schema_version": 1, "world": "feyhin", "passages": [] })", err), "monde inconnu refuse");
	Check(!net.AddFromText("{ pas du json", err) && net.Count() == 4, "JSON invalide : rien ajoute");

	const DoorPassage* so = net.Find(CellMap::Royaumes, "oracle_so_entree");
	const DoorPassage* e = net.Find(CellMap::Royaumes, "oracle_e_entree");
	Check(so && so->requiredQuests.size() == 1 && so->requiredQuests[0] == "oracle_appel", "porte SO : quete exigee");
	Check(e && e->requiredQuests.size() == 2 && e->minGroup == 3, "porte E : deux quetes et raid de 3");
	Check(net.Find(CellMap::Enfers, "oracle_so_entree") == nullptr, "passage propre a sa carte");

	// Conditions.
	std::set<std::string> quests;
	auto has = [&](std::string_view q) { return quests.count(std::string(q)) != 0; };
	Check(so && !DoorPassages::ConditionsMet(*so, has, 1), "SO : refus sans la quete");
	quests.insert("oracle_appel");
	Check(so && DoorPassages::ConditionsMet(*so, has, 1), "SO : passe avec la quete, seul");
	quests.insert("oracle_echos");
	Check(e && !DoorPassages::ConditionsMet(*e, has, 2), "E : refus a deux");
	Check(e && DoorPassages::ConditionsMet(*e, has, 3), "E : passe a trois");
	const DoorPassage* out = net.Find(CellMap::Royaumes, "oracle_so_sortie");
	Check(out && DoorPassages::ConditionsMet(*out, nullptr, 0), "sortie sans condition");

	// Déroulé : connexion (désarmé), réarmement, refus signalé une fois, passage.
	bool allow = false;
	auto allowed = [&](const DoorPassage&) { return allow; };
	DoorPassageTravelState st;
	Check(net.Update(st, CellMap::Royaumes, -50.0, 50.0, true, 0, allowed).outcome == DoorPassageOutcome::None, "hors rayon");
	Check(net.Update(st, CellMap::Royaumes, -50.0, 50.0, true, 5000, allowed).outcome == DoorPassageOutcome::None && st.armed,
		"rearme apres 5 s");
	auto r = net.Update(st, CellMap::Royaumes, -100.0, 101.0, true, 5100, allowed);
	Check(r.outcome == DoorPassageOutcome::Denied && r.passage == so, "refus signale");
	r = net.Update(st, CellMap::Royaumes, -100.5, 100.5, true, 5200, allowed);
	Check(r.outcome == DoorPassageOutcome::None, "refus signale une seule fois par entree");
	allow = true;
	r = net.Update(st, CellMap::Royaumes, -100.0, 100.0, true, 5300, allowed);
	Check(r.outcome == DoorPassageOutcome::Pass && r.passage == so && !st.armed, "passage une fois la quete acceptee");
	Check(r.passage && std::fabs(r.passage->toX - (-30.0)) < 1e-9 && std::fabs(r.passage->toY - 22.2) < 1e-9, "point d'arrivee");
	// Arrivée : hors du rayon de la sortie ; pas de renvoi immédiat (désarmé).
	Check(net.Update(st, CellMap::Royaumes, -32.0, 32.0, true, 5400, allowed).outcome == DoorPassageOutcome::None,
		"pas de renvoi immediat sur la sortie");
	// Mort : jamais de passage.
	DoorPassageTravelState dead;
	dead.armed = true;
	Check(net.Update(dead, CellMap::Royaumes, -100.0, 100.0, false, 0, allowed).outcome == DoorPassageOutcome::None,
		"joueur mort : aucun passage");
	// Refus, sortie du rayon, retour : le refus est de nouveau signalé.
	allow = false;
	DoorPassageTravelState again;
	again.armed = true;
	Check(net.Update(again, CellMap::Royaumes, 140.0, 0.0, true, 0, allowed).outcome == DoorPassageOutcome::Denied, "refus E");
	Check(net.Update(again, CellMap::Royaumes, 150.0, 0.0, true, 10, allowed).outcome == DoorPassageOutcome::None, "sortie du rayon");
	Check(net.Update(again, CellMap::Royaumes, 140.0, 0.0, true, 20, allowed).outcome == DoorPassageOutcome::Denied,
		"nouveau refus apres etre sorti du rayon");
	Check(net.Update(again, CellMap::Royaumes, NAN, 0.0, true, 30, allowed).outcome == DoorPassageOutcome::None,
		"position invalide ignoree");

	if (g_fail == 0) std::printf("door_passages_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
