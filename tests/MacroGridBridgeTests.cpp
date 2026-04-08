#include "engine/world/MacroGridBridge.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Fail(const char* msg)
{
	std::cerr << "FAIL: " << msg << '\n';
	std::exit(1);
}

} // namespace

int main()
{
	int32_t r = 0, c = 0;
	std::string err;

	if (!engine::world::ParseMacroCellId("R050C010", r, c, err))
		Fail("parse R050C010");
	if (r != 50 || c != 10)
		Fail("R050C010 values");

	// Littéral ASCII explicite (évite tout souci d’encodage source sur l’agent CI).
	static constexpr char kR0C0[] = { '\x52', '\x30', '\x43', '\x30' };
	if (!engine::world::ParseMacroCellId(std::string_view(kR0C0, sizeof kR0C0), r, c, err))
	{
		std::cerr << err << '\n';
		Fail("parse R0C0");
	}
	if (r != 0 || c != 0)
		Fail("R0C0 values");

	if (engine::world::ParseMacroCellId("R05C", r, c, err))
		Fail("reject incomplete");
	if (engine::world::ParseMacroCellId("X05C010", r, c, err))
		Fail("reject bad prefix");

	constexpr float kL = 10000.0f;
	const engine::world::MacroCellAabb aabb = engine::world::MacroCellAabbMeters(2, 5, kL);
	if (std::fabs(aabb.minX - 50000.0f) > 0.01f || std::fabs(aabb.maxX - 60000.0f) > 0.01f)
		Fail("macro AABB X");
	if (std::fabs(aabb.minZ - 20000.0f) > 0.01f || std::fabs(aabb.maxZ - 30000.0f) > 0.01f)
		Fail("macro AABB Z");

	std::vector<engine::world::GlobalChunkCoord> chunks;
	engine::world::ChunksIntersectingMacroAabb(aabb, static_cast<float>(engine::world::kChunkSize), chunks);
	const int32_t span = static_cast<int32_t>(std::ceil(kL / static_cast<float>(engine::world::kChunkSize)));
	const size_t expected = static_cast<size_t>(span) * static_cast<size_t>(span);
	if (chunks.size() != expected)
		Fail("chunk count for 10km macro cell");

	const engine::world::MacroCellAabb corner = engine::world::MacroCellAabbMeters(0, 0, 256.0f);
	chunks.clear();
	engine::world::ChunksIntersectingMacroAabb(corner, static_cast<float>(engine::world::kChunkSize), chunks);
	if (chunks.size() != 1u || chunks[0].x != 0 || chunks[0].z != 0)
		Fail("single chunk corner");

	std::cerr << "macro_grid_bridge_tests: OK\n";
	return 0;
}

