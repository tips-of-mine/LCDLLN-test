/// Tests CPU pour DeleteEntitiesOps (remove/restore par index, undo exact).
#include "src/world_editor/scene/DeleteEntitiesOps.h"
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using namespace engine::editor::scene;

	void Test_RemoveRestoreExact()
	{
		std::vector<std::string> v = {"a", "b", "c", "d", "e"};
		// Supprime indices {1,3} (b,d). Ordre d'entrée volontairement non trié.
		auto removed = RemoveByIndexDescending(v, {3, 1});
		REQUIRE(v.size() == 3);
		REQUIRE(v[0] == "a"); REQUIRE(v[1] == "c"); REQUIRE(v[2] == "e");
		// Snapshot trié croissant : (1,b),(3,d).
		REQUIRE(removed.size() == 2);
		REQUIRE(removed[0].first == 1u); REQUIRE(removed[0].second == "b");
		REQUIRE(removed[1].first == 3u); REQUIRE(removed[1].second == "d");

		// Restore -> état initial exact.
		RestoreByIndexAscending(v, removed);
		REQUIRE(v.size() == 5);
		REQUIRE(v[0] == "a"); REQUIRE(v[1] == "b"); REQUIRE(v[2] == "c");
		REQUIRE(v[3] == "d"); REQUIRE(v[4] == "e");
	}

	void Test_OutOfRangeIgnored()
	{
		std::vector<int> v = {10, 20};
		auto removed = RemoveByIndexDescending(v, {5, 0}); // 5 hors borne -> ignoré
		REQUIRE(v.size() == 1);
		REQUIRE(v[0] == 20);
		REQUIRE(removed.size() == 1);
		REQUIRE(removed[0].first == 0u);
		REQUIRE(removed[0].second == 10);
	}
}

int main()
{
	Test_RemoveRestoreExact();
	Test_OutOfRangeIgnored();
	if (g_failed == 0) { std::printf("[PASS] DeleteEntitiesOpsTests\n"); return 0; }
	std::printf("[FAIL] DeleteEntitiesOpsTests: %d failure(s)\n", g_failed);
	return 1;
}
