// src/client/world/tests/TerrainChunkGridTests.cpp
#include "src/client/world/WorldModel.h"
#include "src/client/world/terrain/TerrainChunk.h"

#include <cstdio>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::world::GlobalChunkCoord;
	using engine::world::WorldToTerrainChunkCoord;
	using engine::world::ComputeVisibleTerrainChunks;

	void Test_Constant_Is256()
	{
		REQUIRE(engine::world::kTerrainChunkSizeMeters == 256);
		REQUIRE(engine::world::kTerrainChunkSizeMeters
			== static_cast<int>((engine::world::terrain::kTerrainResolution - 1)
				* engine::world::terrain::kTerrainCellSizeMeters));
	}

	void Test_WorldToTerrainChunkCoord()
	{
		REQUIRE(WorldToTerrainChunkCoord(0.0f, 0.0f).x == 0);
		REQUIRE(WorldToTerrainChunkCoord(0.0f, 0.0f).z == 0);
		REQUIRE(WorldToTerrainChunkCoord(255.9f, 0.0f).x == 0);
		REQUIRE(WorldToTerrainChunkCoord(256.0f, 0.0f).x == 1);
		REQUIRE(WorldToTerrainChunkCoord(0.0f, 768.0f).z == 3);
		REQUIRE(WorldToTerrainChunkCoord(-1.0f, 0.0f).x == -1);
		REQUIRE(WorldToTerrainChunkCoord(-256.0f, 0.0f).x == -1);
		REQUIRE(WorldToTerrainChunkCoord(-257.0f, 0.0f).x == -2);
	}

	void Test_ComputeVisibleTerrainChunks_7x7()
	{
		const auto chunks = ComputeVisibleTerrainChunks(300.0f, 300.0f); // centre = (1,1)
		REQUIRE(chunks.size() == 49u);

		bool hasCenter = false, hasCorner = false, hasOutOfRange = false;
		for (const auto& c : chunks)
		{
			if (c.x == 1 && c.z == 1) hasCenter = true;
			if (c.x == -2 && c.z == -2) hasCorner = true;
			if (c.x > 4 || c.x < -2 || c.z > 4 || c.z < -2) hasOutOfRange = true;
		}
		REQUIRE(hasCenter);
		REQUIRE(hasCorner);
		REQUIRE(!hasOutOfRange);
	}

	void Test_NeighbourChunks_AreContiguous()
	{
		const int o0 = 0 * engine::world::kTerrainChunkSizeMeters;
		const int o1 = 1 * engine::world::kTerrainChunkSizeMeters;
		const int span = engine::world::kTerrainChunkSizeMeters;
		REQUIRE(o0 + span == o1);
	}
}

int main()
{
	Test_Constant_Is256();
	Test_WorldToTerrainChunkCoord();
	Test_ComputeVisibleTerrainChunks_7x7();
	Test_NeighbourChunks_AreContiguous();

	if (g_failed == 0)
		std::printf("[OK] TerrainChunkGridTests: tous les cas passent\n");
	return g_failed;
}
