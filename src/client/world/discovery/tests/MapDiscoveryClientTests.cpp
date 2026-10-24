// Carte à découvrir (client) : révélation au fil du déplacement, fusion des
// pages du master, cellules modifiées à envoyer puis remises en attente.

#include "src/client/world/discovery/MapDiscoveryClient.h"

#include <cstdio>

namespace disc = engine::world::discovery;
namespace cells = engine::world::cells;
using engine::client::MapDiscoveryClient;

namespace
{
	int g_fail = 0;
	void Check(bool cond, const char* msg)
	{
		if (!cond) { std::printf("FAIL: %s\n", msg); ++g_fail; }
	}
}

int main()
{
	const cells::CellCoord c{ 3, -9 };
	const cells::CellPoint ctr = cells::CellCenter(c);

	MapDiscoveryClient d;
	Check(!d.IsDiscovered(ctr.x, ctr.z), "rien au depart");
	const int first = d.RevealAround(ctr.x, ctr.z);
	Check(first > 10, "revelation initiale");
	Check(d.IsDiscovered(ctr.x, ctr.z) && d.IsDiscovered(ctr.x + 200.0, ctr.z), "autour du joueur");
	Check(!d.IsDiscovered(ctr.x + 600.0, ctr.z), "au-dela du rayon : cache");
	Check(d.RevealAround(ctr.x + 10.0, ctr.z) == 0, "meme case : pas de recalcul");
	Check(d.RevealAround(ctr.x + 400.0, ctr.z) > 0, "deplacement : nouvelles cases");
	Check(d.HasDirty() && d.CellCount() == 1, "une cellule modifiee");

	// Envoi : la cellule sort de la liste, puis y revient si l'envoi échoue.
	auto batch = d.TakeDirty(16);
	Check(batch.size() == 1 && batch[0].cellZoneId == cells::CellZoneId(c) && !d.HasDirty(), "TakeDirty");
	d.MarkDirty(batch);
	Check(d.HasDirty(), "MarkDirty");

	// Page du master : fusion OU ; la cellule déjà couverte par le serveur n'est plus à renvoyer.
	engine::network::MapDiscoveryResponsePayload page;
	page.nextIndex = 16;
	engine::network::MapDiscoveryCell remote;
	remote.cellZoneId = cells::CellZoneId({ 4, -4 });
	remote.bitmap.Set(40, 40);
	page.cells.push_back(remote);
	engine::network::MapDiscoveryCell same;
	same.cellZoneId = cells::CellZoneId(c);
	same.bitmap = *d.Find(c);
	page.cells.push_back(same);
	const auto next = d.OnServerPage(page);
	Check(next && *next == 16, "page suivante demandee");
	const cells::CellPoint remoteCtr = disc::TileCenter({ 4, -4 }, 40, 40);
	Check(d.IsDiscovered(remoteCtr.x, remoteCtr.z), "case recue du master visible");
	Check(!d.HasDirty(), "cellule identique au serveur : rien a renvoyer");

	page.nextIndex = 0;
	page.cells.clear();
	Check(!d.OnServerPage(page), "derniere page");
	page.error = 7;
	Check(!d.OnServerPage(page), "erreur : arret");

	d.Reset();
	Check(d.CellCount() == 0 && !d.HasDirty() && d.RevealAround(ctr.x, ctr.z) > 0, "Reset puis nouvelle revelation");

	if (g_fail == 0) std::printf("map_discovery_client_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
