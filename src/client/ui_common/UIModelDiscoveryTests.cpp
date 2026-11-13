// Lore v1.0.114 — réception de DiscoveryState (105) et DiscoveryEvent (106) dans UIModel :
// compteur des cimetières, lieux secrets découverts (liste remise à zéro par le premier
// paquet de l'état), file des événements et lignes de chat système.
//
// Style non-strippable (cerr/compteur/return 1) : rien n'est retiré en build NDEBUG.

#include "src/client/ui_common/UIModel.h"
#include "src/shared/network/ServerProtocol.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	/// Compte un échec et l'affiche si \p condition est faux.
	void Check(bool condition, const char* label)
	{
		if (!condition)
		{
			std::cerr << "[FAIL] " << label << "\n";
			++g_failures;
		}
	}

	/// Lieu de test d'identifiant \p id.
	engine::server::DiscoveredPlaceWire Place(const std::string& id)
	{
		engine::server::DiscoveredPlaceWire p;
		p.id = id;
		p.name = "Nom " + id;
		p.kind = "grotte";
		p.x = 10.0f;
		p.z = -20.0f;
		return p;
	}
}

int main()
{
	using namespace engine::server;
	engine::client::UIModelBinding binding;
	binding.Init();

	// État en 2 paquets (4 lieux) : liste complète, compteur.
	std::vector<DiscoveredPlaceWire> places = { Place("a"), Place("b"), Place("c"), Place("d") };
	for (const auto& packet : EncodeDiscoveryStatePackets(124u, 666u, places))
		Check(binding.ApplyPacket(packet), "ApplyPacket(DiscoveryState)");
	const engine::client::UIDiscoveryState& d = binding.GetModel().discovery;
	Check(d.known && d.graveyardsVisited == 124u && d.graveyardsTotal == 666u && d.places.size() == 4u,
		"état complet reçu");

	// Nouvel état (reconnexion) : la liste repart de zéro.
	for (const auto& packet : EncodeDiscoveryStatePackets(1u, 666u, { Place("z") }))
		Check(binding.ApplyPacket(packet), "ApplyPacket(DiscoveryState) bis");
	Check(d.places.size() == 1u && d.places[0].id == "z" && d.graveyardsVisited == 1u, "premier paquet : remise à zéro");

	// Événements : compteur, lieu ajouté une fois, file.
	DiscoveryEventMessage visit{};
	visit.kind = DiscoveryEventKind::GraveyardVisited;
	visit.graveyardsVisited = 2u;
	visit.graveyardsTotal = 666u;
	visit.id = "hameau_marn";
	Check(binding.ApplyPacket(EncodeDiscoveryEvent(visit)), "ApplyPacket(DiscoveryEvent visite)");
	DiscoveryEventMessage found{};
	found.kind = DiscoveryEventKind::PlaceDiscovered;
	found.graveyardsVisited = 2u;
	found.graveyardsTotal = 666u;
	found.id = "ville_thurvhal";
	found.place = Place("ville_thurvhal");
	found.place.name = "Thurvhal";
	Check(binding.ApplyPacket(EncodeDiscoveryEvent(found)), "ApplyPacket(DiscoveryEvent lieu)");
	Check(binding.ApplyPacket(EncodeDiscoveryEvent(found)), "ApplyPacket(DiscoveryEvent lieu, doublon)");
	Check(d.graveyardsVisited == 2u && d.places.size() == 2u, "compteur à jour, lieu ajouté une seule fois");

	DiscoveryEventMessage exploit{};
	exploit.kind = DiscoveryEventKind::ExploitUnlocked;
	exploit.graveyardsVisited = 666u;
	exploit.graveyardsTotal = 666u;
	exploit.id = "exploit_pelerin_des_666_tombes";
	exploit.title = "Le Pèlerin des Six Cent Soixante-Six Tombes";
	Check(binding.ApplyPacket(EncodeDiscoveryEvent(exploit)), "ApplyPacket(DiscoveryEvent exploit)");

	const auto events = binding.TakeDiscoveryEvents();
	Check(events.size() == 4u && binding.TakeDiscoveryEvents().empty(), "file des événements vidée par Take");
	if (events.size() == 4u)
	{
		Check(engine::client::FormatDiscoveryEventLine(events[0]) == "Cimetière visité (2/666)", "ligne cimetière");
		Check(engine::client::FormatDiscoveryEventLine(events[1]) == "Lieu découvert : Thurvhal", "ligne lieu");
		Check(engine::client::FormatDiscoveryEventLine(events[3])
			== "Exploit débloqué : Le Pèlerin des Six Cent Soixante-Six Tombes", "ligne exploit");
	}

	if (g_failures == 0)
		std::cout << "[OK] ui_model_discovery_tests\n";
	return g_failures == 0 ? 0 : 1;
}
