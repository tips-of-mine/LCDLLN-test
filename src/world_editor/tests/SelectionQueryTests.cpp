/// Tests CPU pour SelectionQuery (encode/décode, pick nearest, rect/lasso).
#include "src/world_editor/scene/SelectionQuery.h"
#include <cstdio>

namespace
{
	int g_failed = 0;
	#define REQUIRE(cond) do { if (!(cond)) { \
		std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

	using namespace engine::editor::scene;
	using namespace engine::editor::world;

	void Test_EncodeRoundTrip()
	{
		const EntityId id{EntityKind::MeshInsert, 1234567};
		const uint32_t enc = EncodeEntityId(id);
		const EntityId dec = DecodeEntityId(enc);
		REQUIRE(dec.kind == EntityKind::MeshInsert);
		REQUIRE(dec.index == 1234567u);
	}

	void Test_PickNearest()
	{
		std::vector<SelectableEntity> pts = {
			{ {EntityKind::LayoutInstance, 0}, 0.f, 0.f },
			{ {EntityKind::LayoutInstance, 1}, 10.f, 0.f },
			{ {EntityKind::LayoutInstance, 2}, 0.f, 10.f },
		};
		// Clic près de (9,0) avec rayon 3 -> index 1.
		auto hit = PickNearest(pts, 9.f, 0.f, 3.f);
		REQUIRE(hit.has_value());
		REQUIRE(hit->index == 1u);
		// Clic loin de tout (rayon 1) -> rien.
		REQUIRE(!PickNearest(pts, 50.f, 50.f, 1.f).has_value());
	}

	void Test_RectAndLasso()
	{
		std::vector<SelectableEntity> pts = {
			{ {EntityKind::LayoutInstance, 0}, 1.f, 1.f },
			{ {EntityKind::LayoutInstance, 1}, 5.f, 5.f },
			{ {EntityKind::LayoutInstance, 2}, 20.f, 20.f },
		};
		SelectionRect rect; rect.minX = 0.f; rect.minZ = 0.f; rect.maxX = 10.f; rect.maxZ = 10.f;
		auto inRect = PickInRect(pts, rect);
		REQUIRE(inRect.size() == 2); // 0 et 1
		// Lasso = même carré 0..10 -> mêmes 2 entités.
		std::vector<std::pair<float, float>> poly = {{0,0},{10,0},{10,10},{0,10}};
		auto inLasso = PickInLasso(pts, poly);
		REQUIRE(inLasso.size() == 2);
	}
}

int main()
{
	Test_EncodeRoundTrip();
	Test_PickNearest();
	Test_RectAndLasso();
	if (g_failed == 0) { std::printf("[PASS] SelectionQueryTests\n"); return 0; }
	std::printf("[FAIL] SelectionQueryTests: %d failure(s)\n", g_failed);
	return 1;
}
