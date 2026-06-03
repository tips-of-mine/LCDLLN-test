/// Tests unitaires CPU pour TerrainDocument (sous-projet 1 — boucle d'édition
/// d'une zone). Cible :
///   - InitFlatZone : alloue N×N chunks plats 257², à la hauteur demandée,
///     tous marqués dirty.
///   - Round-trip disque : InitFlatZone → édition d'une hauteur → SaveDirtyToDisk
///     → relecture via EnsureLoaded depuis un nouveau document → égalité des
///     hauteurs. Garantit que les chunks (source de vérité de la zone)
///     survivent au cycle sauver/recharger.
///
/// Pas de dépendance ImGui ni Vulkan : pur CPU + E/S disque (FileSystem +
/// sérialisation terrain.bin). Tourne en CI Linux sous ctest.

#include "src/shared/core/Config.h"
#include "src/world_editor/terrain/TerrainDocument.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/WorldModel.h"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::editor::world::TerrainDocument;

	/// Répertoire temporaire unique (créé). Abort si la création échoue.
	std::filesystem::path MakeTempContentDir()
	{
		std::random_device rd;
		std::mt19937_64 rng(rd());
		const std::filesystem::path base = std::filesystem::temp_directory_path()
			/ ("lcdlln_terrain_doc_test_" + std::to_string(rng()));
		std::error_code ec;
		std::filesystem::create_directories(base, ec);
		if (ec) { std::abort(); }
		return base;
	}

	/// InitFlatZone alloue NxN chunks plats à la hauteur demandée, tous dirty.
	void Test_InitFlatZoneAllocatesNxNFlatChunks()
	{
		TerrainDocument doc;
		doc.InitFlatZone(/*chunksPerAxis=*/4, /*flatHeightMeters=*/0.0f);
		REQUIRE(doc.LoadedChunkCount() == 16u);

		auto c = doc.Find(engine::world::GlobalChunkCoord{0, 0});
		REQUIRE(c != nullptr);
		REQUIRE(c->resolutionX == 257u);
		REQUIRE(c->resolutionZ == 257u);
		REQUIRE(c->heights.size() == 257u * 257u);
		bool allZero = true;
		for (float h : c->heights) { if (h != 0.0f) { allZero = false; break; } }
		REQUIRE(allZero);

		// Coin opposé présent aussi.
		REQUIRE(doc.Find(engine::world::GlobalChunkCoord{3, 3}) != nullptr);
		// Tout neuf = dirty -> sera persisté.
		REQUIRE(doc.HasDirtyChunks());
	}

	/// Round-trip disque : édition d'une hauteur -> save -> reload identique.
	void Test_ChunkSaveLoadRoundTrip()
	{
		const std::filesystem::path tmp = MakeTempContentDir();
		engine::core::Config cfg;
		cfg.SetValue("paths.content", engine::core::Config::Value{ tmp.string() });

		TerrainDocument doc;
		doc.InitFlatZone(2, 0.0f);
		auto c = doc.Find(engine::world::GlobalChunkCoord{1, 0});
		REQUIRE(c != nullptr);
		const size_t idx = 137u * 257u + 42u; // cellule arbitraire à l'intérieur
		c->heights[idx] = 5.0f;
		c->RecomputeBounds();
		doc.MarkDirty(engine::world::GlobalChunkCoord{1, 0});

		const size_t written = doc.SaveDirtyToDisk(cfg);
		REQUIRE(written >= 1u);

		// Nouveau document : recharge le chunk depuis le disque.
		TerrainDocument doc2;
		auto c2 = doc2.EnsureLoaded(cfg, 1, 0);
		REQUIRE(c2 != nullptr);
		REQUIRE(c2->heights.size() == 257u * 257u);
		REQUIRE(c2->heights[idx] == 5.0f);

		std::error_code ec;
		std::filesystem::remove_all(tmp, ec);
	}
}

int main()
{
	Test_InitFlatZoneAllocatesNxNFlatChunks();
	Test_ChunkSaveLoadRoundTrip();

	if (g_failed == 0)
	{
		std::printf("[PASS] TerrainDocumentRoundTripTests\n");
		return 0;
	}
	std::printf("[FAIL] TerrainDocumentRoundTripTests: %d failure(s)\n", g_failed);
	return 1;
}
