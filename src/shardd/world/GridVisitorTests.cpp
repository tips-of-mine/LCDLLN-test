// Wave 18 — GridVisitor tests : pattern Visitor sur CellGrid (AoI 7x7).
// Tests deterministes (pas de random stat) : place les entites a des
// positions connues + verifie le set d'IDs visites.
//
// Pattern aligne sur ObjectGuidTests.cpp / WorldObjectTests.cpp :
// asserts + printf, pas de framework. Cible CTest : grid_visitor_tests.

#include "src/shardd/world/GridVisitor.h"
#include "src/shardd/world/SpatialPartition.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

using namespace engine::server;

namespace
{
	/// Visite vide : aucune entite -> fn jamais appelee.
	void TestGridVisitEmpty()
	{
		CellGrid grid;
		grid.Init();
		int count = 0;
		GridVisit(grid, 5000.0f, 5000.0f, [&count](EntityId) { ++count; });
		assert(count == 0);
		std::puts("[OK] TestGridVisitEmpty");
	}

	/// Une entite a la position center : fn appelee 1x avec son ID.
	void TestGridVisitSingleEntity()
	{
		CellGrid grid;
		grid.Init();
		CellCoord cell{};
		assert(grid.UpsertEntity(/*id*/42, /*x*/5000.0f, /*z*/5000.0f, cell));

		std::vector<EntityId> visited;
		GridVisit(grid, 5000.0f, 5000.0f, [&visited](EntityId id) {
			visited.push_back(id);
		});
		assert(visited.size() == 1);
		assert(visited[0] == 42);
		std::puts("[OK] TestGridVisitSingleEntity");
	}

	/// Entite a +50m (meme cell que center avec kSpatialCellSizeMeters=100) :
	/// visite OK.
	void TestGridVisitSameCell()
	{
		CellGrid grid;
		grid.Init();
		CellCoord cell{};
		assert(grid.UpsertEntity(1, 5000.0f, 5000.0f, cell));
		assert(grid.UpsertEntity(2, 5050.0f, 5050.0f, cell)); // meme cell

		std::vector<EntityId> visited;
		GridVisit(grid, 5000.0f, 5000.0f, [&visited](EntityId id) {
			visited.push_back(id);
		});
		assert(visited.size() == 2);
		std::puts("[OK] TestGridVisitSameCell");
	}

	/// Entite a +200m (cell+2, dans le 7x7 centre sur (5000,5000)) : visite OK.
	/// Entite a +500m (cell+5, hors 7x7) : non visitee.
	void TestGridVisitNeighborhoodBoundary()
	{
		CellGrid grid;
		grid.Init();
		CellCoord cell{};
		assert(grid.UpsertEntity(10, 5000.0f, 5000.0f, cell));  // center
		assert(grid.UpsertEntity(20, 5200.0f, 5000.0f, cell));  // +2 cells (inside 7x7)
		assert(grid.UpsertEntity(30, 5500.0f, 5000.0f, cell));  // +5 cells (outside 7x7)

		std::vector<EntityId> visited;
		GridVisit(grid, 5000.0f, 5000.0f, [&visited](EntityId id) {
			visited.push_back(id);
		});
		assert(visited.size() == 2);
		// 10 et 20 sont dans le 7x7, 30 est dehors.
		auto has10 = std::find(visited.begin(), visited.end(), 10) != visited.end();
		auto has20 = std::find(visited.begin(), visited.end(), 20) != visited.end();
		auto has30 = std::find(visited.begin(), visited.end(), 30) != visited.end();
		assert(has10 && has20 && !has30);
		std::puts("[OK] TestGridVisitNeighborhoodBoundary");
	}

	/// Center hors de la zone : no-op silent (pas de crash, pas de visite).
	void TestGridVisitOutsideZone()
	{
		CellGrid grid;
		grid.Init();
		CellCoord cell{};
		assert(grid.UpsertEntity(1, 5000.0f, 5000.0f, cell));

		// Position negative ou > kZoneSizeMeters -> hors zone.
		int count = 0;
		GridVisit(grid, -100.0f, 5000.0f, [&count](EntityId) { ++count; });
		assert(count == 0);
		GridVisit(grid, 50000.0f, 5000.0f, [&count](EntityId) { ++count; });
		assert(count == 0);
		std::puts("[OK] TestGridVisitOutsideZone");
	}

	/// Grille non initialisee : GridVisit retourne sans erreur (TryWorldToCellCoord
	/// retourne false).
	void TestGridVisitUninitialized()
	{
		CellGrid grid;
		// pas d'Init()
		int count = 0;
		GridVisit(grid, 5000.0f, 5000.0f, [&count](EntityId) { ++count; });
		assert(count == 0);
		std::puts("[OK] TestGridVisitUninitialized");
	}

	/// Visitor stateful (pattern GridVisitWithVisitor) : la struct
	/// accumule les ids, on verifie l'integrite.
	void TestGridVisitWithVisitor()
	{
		struct CollectorVisitor
		{
			std::vector<EntityId> ids;
			void Visit(EntityId id) { ids.push_back(id); }
		};

		CellGrid grid;
		grid.Init();
		CellCoord cell{};
		assert(grid.UpsertEntity(7, 5000.0f, 5000.0f, cell));
		assert(grid.UpsertEntity(8, 5050.0f, 5050.0f, cell));

		CollectorVisitor v;
		GridVisitWithVisitor(grid, 5000.0f, 5000.0f, v);
		assert(v.ids.size() == 2);
		std::puts("[OK] TestGridVisitWithVisitor");
	}

	/// Stress test : 1000 entites reparties uniformement sur la zone.
	/// Avec AoI 7x7 cells = 700m sur une zone 10000m, on attend en
	/// moyenne (700/10000)^2 * 1000 = 4.9 entites visibles. Test
	/// deterministe : on place les entites sur une grille regulière et
	/// on calcule exactement combien tombent dans le 7x7 centre.
	void TestGridVisitStress1000Entities()
	{
		CellGrid grid;
		grid.Init();

		// Place 1000 entites sur grille 32x32 (1024 cells, gardons 1000)
		// pas = 312.5m, mais on arrondit a 310m pour rester dans zone.
		// Cells 100m -> les entites tombent dans des cells distinctes.
		const int stepMeters = 310;
		int placed = 0;
		for (int gx = 0; gx < 32 && placed < 1000; ++gx)
		{
			for (int gz = 0; gz < 32 && placed < 1000; ++gz)
			{
				const float x = static_cast<float>(gx * stepMeters + 50);
				const float z = static_cast<float>(gz * stepMeters + 50);
				CellCoord cell{};
				if (grid.UpsertEntity(static_cast<EntityId>(placed + 1), x, z, cell))
					++placed;
			}
		}
		assert(placed == 1000);

		// Visite autour de (5000, 5000) : 7x7 cells centrees sur (50, 50)
		// in cell-coords (5000m / 100m = 50). Range cells [47..53] sur x et z.
		// Cells touchées : trouver les entites avec gx*310+50 dans [4700..5350]
		// (cells 47..53 = 4700..5400 m), idem pour z. gx*310+50 in [4700..5350]
		// -> gx in [15..17] (since 15*310+50=4700, 17*310+50=5320, 18*310+50=5630>5400).
		// 3 valeurs de gx * 3 valeurs de gz = 9 entites attendues.
		int count = 0;
		GridVisit(grid, 5000.0f, 5000.0f, [&count](EntityId) { ++count; });
		assert(count == 9);
		std::puts("[OK] TestGridVisitStress1000Entities");
	}
}

int main()
{
	TestGridVisitEmpty();
	TestGridVisitSingleEntity();
	TestGridVisitSameCell();
	TestGridVisitNeighborhoodBoundary();
	TestGridVisitOutsideZone();
	TestGridVisitUninitialized();
	TestGridVisitWithVisitor();
	TestGridVisitStress1000Entities();
	std::puts("All GridVisitor tests passed");
	return 0;
}
