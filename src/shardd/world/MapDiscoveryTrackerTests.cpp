// ETAPE2.5 — tests de MapDiscoveryTracker (carte à découvrir côté shard).
// Pattern repo : int main() + vérifications + code retour.

#include "src/shardd/world/MapDiscoveryTracker.h"

#include <cstdio>

namespace
{
	int s_failCount = 0;

	void Check(bool cond, const char* msg)
	{
		if (!cond)
		{
			++s_failCount;
			std::fprintf(stderr, "[FAIL] %s\n", msg);
		}
	}
}

int main()
{
	using engine::server::MapDiscoveryTracker;
	namespace cells = engine::world::cells;
	namespace disc = engine::world::discovery;

	MapDiscoveryTracker tracker;
	Check(tracker.CellCount() == 0u, "vide au départ");

	// 1. Première révélation au centre de la cellule de l'Oracle (0, 0).
	const int first = tracker.RevealAround(0.0, 0.0);
	Check(first > 0, "des cases sont révélées");
	Check(tracker.CellCount() == 1u, "une seule cellule touchée au centre");

	// 2. Même case : rien de recalculé.
	Check(tracker.RevealAround(10.0, 10.0) == 0, "même case : rien de nouveau");

	// 3. Le masque contient bien la case du joueur.
	const auto session = tracker.SessionCells();
	Check(session.size() == 1u && session[0].first == cells::CellZoneId(cells::CellCoord{ 0, 0 }), "identifiant de cellule");
	if (!session.empty())
	{
		const disc::TileRef here = disc::TileAt(0.0, 0.0);
		Check(session[0].second.Get(here.tx, here.tz), "case du joueur découverte");
		Check(session[0].second.Count() == first, "compte des cases = révélées");
	}

	// 4. Près d'un bord : deux cellules touchées, triées par identifiant.
	tracker.RevealAround(4950.0, 0.0);
	const auto two = tracker.SessionCells();
	Check(two.size() == 2u, "bord de cellule : la voisine est touchée");
	Check(two.size() == 2u && two[0].first < two[1].first, "tri par identifiant");

	// 5. Reset : nouvelle session.
	tracker.Reset();
	Check(tracker.CellCount() == 0u, "Reset vide le traceur");
	Check(tracker.RevealAround(0.0, 0.0) == first, "après Reset, mêmes cases révélées");

	if (s_failCount == 0)
		std::puts("[OK] map_discovery_tracker_tests");
	return s_failCount == 0 ? 0 : 1;
}
