// src/client/render/tests/TerrainRenderSelectionTests.cpp
#include "src/client/render/terrain/TerrainRenderSelection.h"

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

	using engine::render::ShouldDrawLegacyTerrain;

	// Carte heightmap-only : aucun chunk dessiné -> on DOIT dessiner le legacy.
	void Test_NoChunks_DrawsLegacy()
	{
		REQUIRE(ShouldDrawLegacyTerrain(/*legacyValid=*/true, /*hasLoadPass=*/true, /*chunkCount=*/0u) == true);
	}

	// Carte chunkée : des chunks dessinés -> on N'AFFICHE PAS le legacy (anti z-fight).
	void Test_ChunksCover_HidesLegacy()
	{
		REQUIRE(ShouldDrawLegacyTerrain(true, true, 5u) == false);
	}

	// Borne : un seul chunk suffit à masquer le legacy.
	void Test_OneChunk_HidesLegacy()
	{
		REQUIRE(ShouldDrawLegacyTerrain(true, true, 1u) == false);
	}

	// Legacy invalide -> jamais dessiné, peu importe le reste.
	void Test_LegacyInvalid_NeverDraws()
	{
		REQUIRE(ShouldDrawLegacyTerrain(false, true, 0u) == false);
	}

	// Pas de LOAD pass disponible -> on ne dessine pas le legacy ici.
	void Test_NoLoadPass_NeverDraws()
	{
		REQUIRE(ShouldDrawLegacyTerrain(true, false, 0u) == false);
	}
}

int main()
{
	Test_NoChunks_DrawsLegacy();
	Test_ChunksCover_HidesLegacy();
	Test_OneChunk_HidesLegacy();
	Test_LegacyInvalid_NeverDraws();
	Test_NoLoadPass_NeverDraws();

	if (g_failed == 0)
		std::printf("[OK] TerrainRenderSelectionTests: tous les cas passent\n");
	return g_failed;
}
