/// Tests CPU pour EntityKey (clé stable de calque). Pur, ctest Linux.
#include "src/world_editor/scene/EntityKey.h"
#include <cstdio>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using namespace engine::editor::scene;

	void Test_KeyStableAndDistinct()
	{
		// Déterministe : même entrée -> même clé.
		REQUIRE(MakeEntityKeyFromString(EntityKind::LayoutInstance, "tree_oak_001")
			== MakeEntityKeyFromString(EntityKind::LayoutInstance, "tree_oak_001"));
		// Guid différent -> clé différente.
		REQUIRE(MakeEntityKeyFromString(EntityKind::LayoutInstance, "tree_oak_001")
			!= MakeEntityKeyFromString(EntityKind::LayoutInstance, "tree_oak_002"));
		// Même guid, kind différent -> clé différente (préfixe kind).
		REQUIRE(MakeEntityKeyFromString(EntityKind::LayoutInstance, "x")
			!= MakeEntityKeyFromString(EntityKind::MeshInsert, "x"));
		// Variante numérique (mesh/dungeon) : déterministe + séparée par kind.
		REQUIRE(MakeEntityKeyFromGuid(EntityKind::MeshInsert, 42)
			== MakeEntityKeyFromGuid(EntityKind::MeshInsert, 42));
		REQUIRE(MakeEntityKeyFromGuid(EntityKind::MeshInsert, 42)
			!= MakeEntityKeyFromGuid(EntityKind::DungeonPortal, 42));
		// Clé non nulle (0 = "non assigné" côté LayersDocument).
		REQUIRE(MakeEntityKeyFromGuid(EntityKind::MeshInsert, 0) != 0u);
	}
}

int main()
{
	Test_KeyStableAndDistinct();
	if (g_failed == 0) { std::printf("[PASS] EntityKeyTests\n"); return 0; }
	std::printf("[FAIL] EntityKeyTests: %d failure(s)\n", g_failed);
	return 1;
}
