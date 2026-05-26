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
		GridVisit(grid, 0.0f, 0.0f, [&count](EntityId) { ++count; });
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
		GridVisit(grid, 0.0f, 0.0f, [&visited](EntityId id) {
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
		assert(grid.UpsertEntity(1, 0.0f, 0.0f, cell));
		assert(grid.UpsertEntity(2, 50.0f, 50.0f, cell)); // meme cell

		std::vector<EntityId> visited;
		GridVisit(grid, 0.0f, 0.0f, [&visited](EntityId id) {
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
		assert(grid.UpsertEntity(10, 0.0f, 0.0f, cell));  // center
		assert(grid.UpsertEntity(20, 200.0f, 0.0f, cell));  // +2 cells (inside 7x7)
		assert(grid.UpsertEntity(30, 500.0f, 0.0f, cell));  // +5 cells (outside 7x7)

		std::vector<EntityId> visited;
		GridVisit(grid, 0.0f, 0.0f, [&visited](EntityId id) {
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
		assert(grid.UpsertEntity(1, 0.0f, 0.0f, cell));

		// Convention centree : plage monde acceptee [-5000, +5000). Au-dela : hors-zone.
		int count = 0;
		GridVisit(grid, -50000.0f, 0.0f, [&count](EntityId) { ++count; });
		assert(count == 0);
		GridVisit(grid, 50000.0f, 0.0f, [&count](EntityId) { ++count; });
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
		GridVisit(grid, 0.0f, 0.0f, [&count](EntityId) { ++count; });
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
		assert(grid.UpsertEntity(7, 0.0f, 0.0f, cell));
		assert(grid.UpsertEntity(8, 50.0f, 50.0f, cell));

		CollectorVisitor v;
		GridVisitWithVisitor(grid, 0.0f, 0.0f, v);
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

		// Convention centree (-5000..+5000) : place 1000 entites sur grille 32x32 dont
		// l'origine grille est a (-5000+50, -5000+50). Pas = 310m. Cells 100m -> les
		// entites tombent dans des cells distinctes.
		const int stepMeters = 310;
		const float originMeters = -5000.0f + 50.0f; // coin sud-ouest dans le repere centre
		int placed = 0;
		for (int gx = 0; gx < 32 && placed < 1000; ++gx)
		{
			for (int gz = 0; gz < 32 && placed < 1000; ++gz)
			{
				const float x = originMeters + static_cast<float>(gx * stepMeters);
				const float z = originMeters + static_cast<float>(gz * stepMeters);
				CellCoord cell{};
				if (grid.UpsertEntity(static_cast<EntityId>(placed + 1), x, z, cell))
					++placed;
			}
		}
		assert(placed == 1000);

		// Visite autour de (0, 0) world (= cell (50, 50) en repere grille apres offset).
		// Plage cellules visitee : [47..53] sur x et z (7x7). Cellules touchees a partir
		// de la grille reguliere : entites avec gx*310 + (-4950) dans world [-300..+350]
		// (cells 47..53 = world [-300..+400] approx). gx in [15..17] (15*310-4950=-300,
		// 17*310-4950=320). 3 valeurs de gx * 3 valeurs de gz = 9 entites attendues.
		int count = 0;
		GridVisit(grid, 0.0f, 0.0f, [&count](EntityId) { ++count; });
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
