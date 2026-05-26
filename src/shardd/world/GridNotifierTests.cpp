// Wave 18 — GridNotifier tests : foncteur Visitor avec predicate filter
// (typiquement "est un Player"). Branche sur GridVisitor.
//
// Pattern aligne sur les autres tests entities Wave 17 : asserts +
// printf, pas de framework. Cible CTest : grid_notifier_tests.

#include "src/shardd/world/GridNotifier.h"
#include "src/shardd/world/GridVisitor.h"
#include "src/shardd/world/SpatialPartition.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <unordered_set>

using namespace engine::server;

namespace
{
	/// Notifier avec predicate null : aucune collecte (defensif).
	void TestGridNotifierNullPredicate()
	{
		GridNotifier notifier(/*isPlayer*/nullptr);
		notifier.Visit(1);
		notifier.Visit(2);
		assert(notifier.Count() == 0);
		assert(notifier.Recipients().empty());
		std::puts("[OK] TestGridNotifierNullPredicate");
	}

	/// Predicate "tous players" : tous les ids visites sont collectes.
	void TestGridNotifierAllPlayers()
	{
		GridNotifier notifier([](EntityId) { return true; });
		notifier.Visit(10);
		notifier.Visit(20);
		notifier.Visit(30);
		assert(notifier.Count() == 3);
		assert(notifier.Recipients()[0] == 10);
		assert(notifier.Recipients()[1] == 20);
		assert(notifier.Recipients()[2] == 30);
		std::puts("[OK] TestGridNotifierAllPlayers");
	}

	/// Predicate selectif : seules certaines ids sont des players.
	void TestGridNotifierSelectiveFilter()
	{
		std::unordered_set<EntityId> players = {100, 300};
		GridNotifier notifier([&players](EntityId id) {
			return players.count(id) > 0;
		});

		// Visite 100, 200, 300, 400 : seuls 100 et 300 doivent etre collectes.
		notifier.Visit(100);
		notifier.Visit(200);
		notifier.Visit(300);
		notifier.Visit(400);
		assert(notifier.Count() == 2);
		auto has100 = std::find(notifier.Recipients().begin(),
			notifier.Recipients().end(), 100) != notifier.Recipients().end();
		auto has300 = std::find(notifier.Recipients().begin(),
			notifier.Recipients().end(), 300) != notifier.Recipients().end();
		auto has200 = std::find(notifier.Recipients().begin(),
			notifier.Recipients().end(), 200) != notifier.Recipients().end();
		assert(has100);
		assert(has300);
		assert(!has200);
		std::puts("[OK] TestGridNotifierSelectiveFilter");
	}

	/// Reset clear les recipients sans toucher au predicate.
	void TestGridNotifierReset()
	{
		GridNotifier notifier([](EntityId) { return true; });
		notifier.Visit(1);
		notifier.Visit(2);
		assert(notifier.Count() == 2);
		notifier.Reset();
		assert(notifier.Count() == 0);
		// Le predicate fonctionne toujours apres reset.
		notifier.Visit(3);
		assert(notifier.Count() == 1);
		assert(notifier.Recipients()[0] == 3);
		std::puts("[OK] TestGridNotifierReset");
	}

	/// Integration end-to-end : place 4 entites dans une CellGrid, dont 2
	/// "players" (par filter). GridVisitWithVisitor parcourt l'AoI, le
	/// notifier filtre via le predicate.
	void TestGridNotifierIntegrationWithCellGrid()
	{
		CellGrid grid;
		grid.Init();

		// 4 entites au meme endroit (0,0) — convention centree, tous dans le 7x7 center.
		CellCoord cell{};
		assert(grid.UpsertEntity(101, 0.0f, 0.0f, cell));
		assert(grid.UpsertEntity(102, 50.0f, 50.0f, cell));
		assert(grid.UpsertEntity(201, 10.0f, 10.0f, cell));
		assert(grid.UpsertEntity(202, 20.0f, 20.0f, cell));

		// "Players" = ids commencant par 1, "Creatures" = ids commencant par 2.
		std::unordered_set<EntityId> playerIds = {101, 102};
		GridNotifier notifier([&playerIds](EntityId id) {
			return playerIds.count(id) > 0;
		});

		GridVisitWithVisitor(grid, 0.0f, 0.0f, notifier);

		assert(notifier.Count() == 2);
		auto has101 = std::find(notifier.Recipients().begin(),
			notifier.Recipients().end(), 101) != notifier.Recipients().end();
		auto has102 = std::find(notifier.Recipients().begin(),
			notifier.Recipients().end(), 102) != notifier.Recipients().end();
		assert(has101);
		assert(has102);
		std::puts("[OK] TestGridNotifierIntegrationWithCellGrid");
	}
}

int main()
{
	TestGridNotifierNullPredicate();
	TestGridNotifierAllPlayers();
	TestGridNotifierSelectiveFilter();
	TestGridNotifierReset();
	TestGridNotifierIntegrationWithCellGrid();
	std::puts("All GridNotifier tests passed");
	return 0;
}
