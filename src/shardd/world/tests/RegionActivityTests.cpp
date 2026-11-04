// region_activity_tests — activation des régions (cellules de 10 km) du monde « Royaumes ».
#include "src/shardd/world/RegionActivity.h"

#include <cstdio>

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
	using engine::server::RegionActivity;
	using engine::world::cells::CellCoord;

	RegionActivity a; // marge 1000 m
	Check(a.ActiveCount() == 0 && !a.IsActive({ 0, 0 }), "vide au depart");

	// Joueur au centre de l'Oracle : seule sa cellule est active.
	a.Begin();
	a.AddPlayer(0.0, 0.0);
	Check(a.IsActive({ 0, 0 }) && a.IsOccupied({ 0, 0 }), "cellule du joueur active et occupee");
	Check(a.ActiveCount() == 1 && a.OccupiedCount() == 1, "une seule cellule au centre");
	// ETAPE4.3 — liste des cellules actives (entrée du cycle de vie des régions).
	Check(a.ActiveCells().size() == 1u && a.ActiveCells()[0] == CellCoord{ 0, 0 }, "liste des cellules actives");
	Check(!a.IsActive({ 0, 1 }) && !a.IsActive({ 1, 0 }), "voisines inactives au centre");

	// Joueur à 300 m du bord est : la voisine est (pas le nord) devient active, non occupée.
	a.Begin();
	a.AddPlayer(4700.0, 0.0);
	Check(a.IsActive({ 0, 1 }) && !a.IsOccupied({ 0, 1 }), "voisine est activee par la marge");
	Check(!a.IsActive({ 1, 0 }) && !a.IsActive({ 1, 1 }), "nord et nord-est restent inactives");
	Check(a.ActiveCount() == 2 && a.OccupiedCount() == 1, "deux actives, une occupee");

	// Coin nord-est (nord = -Z) : 3 voisines actives.
	a.Begin();
	a.AddPlayer(4800.0, -4800.0);
	Check(a.IsActive({ 1, 0 }) && a.IsActive({ 0, 1 }) && a.IsActive({ 1, 1 }), "coin : trois voisines");
	Check(a.ActiveCount() == 4, "coin : quatre cellules actives");

	// Begin() oublie tout ; deux joueurs dans la même cellule = une seule occupée.
	a.Begin();
	Check(a.ActiveCount() == 0 && !a.IsActive({ 0, 0 }), "Begin efface");
	a.AddPlayer(-90000.0, -30000.0); // hameau de Marn (N003_W009)
	a.AddPlayer(-90100.0, -29900.0);
	Check(a.IsActive({ 3, -9 }) && a.OccupiedCount() == 1 && a.ActiveCount() == 1, "deux joueurs, une cellule");
	Check(a.IsActiveAt(-91000.0, -31000.0) && !a.IsActiveAt(0.0, 0.0), "IsActiveAt");
	Check(a.ActiveCells().size() == 1u && a.ActiveCells()[0] == CellCoord{ 3, -9 }, "liste : coordonnées négatives restituées");

	// Seconde carte : les cellules (0,0) des Royaumes et des Enfers sont distinctes.
	{
		using engine::world::cells::CellMap;
		using engine::world::cells::MapCell;
		RegionActivity m;
		m.AddPlayer(0.0, 0.0, CellMap::Enfers);
		Check(m.IsActive(MapCell({ 0, 0 }, CellMap::Enfers)) && !m.IsActive({ 0, 0 }), "enfers : seule la cellule des enfers est active");
		Check(m.IsActiveAt(10.0, 10.0, CellMap::Enfers) && !m.IsActiveAt(10.0, 10.0), "IsActiveAt par carte");
		m.AddPlayer(0.0, 0.0);
		Check(m.ActiveCount() == 2 && m.OccupiedCount() == 2, "meme (0,0), deux cartes : deux regions");
		m.Begin();
		m.AddPlayer(4700.0, 0.0, CellMap::Enfers);
		Check(m.IsActive(MapCell({ 0, 1 }, CellMap::Enfers)) && !m.IsActive({ 0, 1 }), "voisine activee sur la meme carte");
		bool allEnfers = true;
		for (const MapCell& r : m.ActiveCells()) allEnfers = allEnfers && r.map == CellMap::Enfers;
		Check(allEnfers && m.ActiveCells().size() == 2u, "liste : carte restituee");
	}

	// Marge nulle : jamais de voisine ; marge bornée à la demi-cellule.
	RegionActivity strict(0.0);
	strict.AddPlayer(4999.0, 0.0);
	Check(strict.ActiveCount() == 1, "marge nulle : pas de voisine");
	RegionActivity wide(1e9);
	Check(wide.MarginMeters() == 5000.0, "marge bornee a 5000 m");

	if (g_fail == 0) std::printf("region_activity_tests: OK\n");
	return g_fail == 0 ? 0 : 1;
}
