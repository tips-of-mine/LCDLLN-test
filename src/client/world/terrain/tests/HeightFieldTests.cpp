/// Tests unitaires pour les `IHeightField` de la collision terrain (Phase 2,
/// chantier C).
///
/// Vérifient :
///   - HeightmapHeightField(nullptr) : HeightAt == 0, IsLoadedAt == false
///     (repli sûr quand aucune heightmap n'est branchée — anti-NaN).
///   - ChunkHeightField avec StreamCache résident : on insère un `terrain.bin`
///     synthétique (chunk plat à 42 m) pour la coord terrain (0,0), puis :
///       * IsLoadedAt(10,10)  == true  (chunk (0,0) résident)
///       * IsLoadedAt(300,10) == false (chunk (1,0) absent du cache)
///       * HeightAt(10,10)    ≈ 42     (chaîne monde→chunk→local + SampleHeight)
///   - ChunkHeightField(nullptr cache) : IsLoadedAt false, HeightAt 0.
///
/// L'insertion cache se fait via `StreamCache::Insert` (pas d'I/O disque) sur
/// un blob produit par `SaveTerrainBin` : `LoadFromCache` (lookup pur) le
/// retrouve sans toucher au disque. La grille terrain fait 256 m
/// (`kTerrainChunkSizeMeters`), donc (10,10) tombe dans le chunk (0,0) et
/// (300,10) dans le chunk (1,0).
///
/// HeightmapHeightField n'est testé qu'au cas nullptr : `TerrainRenderer`
/// exige un device Vulkan et ne peut pas être instancié utilement en test CPU.
///
/// Style aligné sur src/client/world/terrain/tests/TerrainChunkTests.cpp
/// (REQUIRE macro maison + main() retournant le nombre d'échecs).

#include "src/client/world/terrain/ChunkHeightField.h"
#include "src/client/world/terrain/HeightmapHeightField.h"
#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/terrain/TerrainChunkLoader.h"
#include "src/client/world/StreamCache.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failed = 0;

	#define REQUIRE(cond) do { \
		if (!(cond)) { \
			std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
			++g_failed; \
		} \
	} while (0)

	using engine::world::StreamCache;
	using engine::world::terrain::ChunkHeightField;
	using engine::world::terrain::HeightmapHeightField;
	using engine::world::terrain::TerrainChunk;
	using engine::world::terrain::SaveTerrainBin;
	using engine::world::terrain::MakeTerrainCacheKey;

	bool ApproxEq(float a, float b, float eps = 1e-3f)
	{
		return std::fabs(a - b) <= eps;
	}

	/// HeightmapHeightField(nullptr) : repli sûr (0 / false), jamais de NaN.
	void Test_HeightmapHeightField_Nullptr_SafeFallback()
	{
		HeightmapHeightField field(nullptr);
		REQUIRE(ApproxEq(field.HeightAt(0.0f, 0.0f), 0.0f));
		REQUIRE(ApproxEq(field.HeightAt(123.0f, -456.0f), 0.0f));
		REQUIRE(field.IsLoadedAt(0.0f, 0.0f) == false);
		REQUIRE(field.IsLoadedAt(123.0f, -456.0f) == false);
	}

	/// ChunkHeightField(nullptr cache) : pas de chunk résident → 0 / false.
	void Test_ChunkHeightField_NullCache_SafeFallback()
	{
		ChunkHeightField field(nullptr, nullptr);
		REQUIRE(field.IsLoadedAt(10.0f, 10.0f) == false);
		REQUIRE(ApproxEq(field.HeightAt(10.0f, 10.0f), 0.0f));
	}

	/// Insère un chunk plat (42 m) en (0,0) dans le cache et vérifie
	/// l'aiguillage résident + l'échantillonnage monde→chunk→local.
	void Test_ChunkHeightField_ResidentChunk_SwitchesAndSamples()
	{
		StreamCache cache; // pas d'Init() nécessaire : capacité défaut 1 GB.

		// Construire un terrain.bin synthétique : chunk plat à 42 m, coord (0,0).
		TerrainChunk flat = TerrainChunk::MakeFlat(42.0f);
		std::vector<uint8_t> blob;
		std::string err;
		REQUIRE(SaveTerrainBin(flat, blob, err));
		REQUIRE(!blob.empty());

		const std::string key = MakeTerrainCacheKey(0, 0);
		cache.Insert(key, blob);

		ChunkHeightField field(&cache, nullptr);

		// (10,10) tombe dans le chunk terrain (0,0) (grille 256 m) → résident.
		REQUIRE(field.IsLoadedAt(10.0f, 10.0f) == true);
		REQUIRE(ApproxEq(field.HeightAt(10.0f, 10.0f), 42.0f));

		// (300,10) tombe dans le chunk (1,0), non inséré → non résident → 0.
		REQUIRE(field.IsLoadedAt(300.0f, 10.0f) == false);
		REQUIRE(ApproxEq(field.HeightAt(300.0f, 10.0f), 0.0f));
	}
}

int main()
{
	Test_HeightmapHeightField_Nullptr_SafeFallback();
	Test_ChunkHeightField_NullCache_SafeFallback();
	Test_ChunkHeightField_ResidentChunk_SwitchesAndSamples();

	if (g_failed == 0)
	{
		std::printf("[PASS] HeightFieldTests (3/3)\n");
		return 0;
	}
	std::printf("[FAIL] HeightFieldTests: %d failure(s)\n", g_failed);
	return g_failed;
}
