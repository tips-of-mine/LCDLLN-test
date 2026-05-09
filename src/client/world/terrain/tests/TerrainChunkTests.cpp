/// Tests unitaires pour engine::world::terrain::TerrainChunk (M100.5).
///
/// Vérifient :
///   - MakeFlat(h) : produit 257² hauteurs uniformes à h, bornes correctes.
///   - SampleHeight bilinéaire : interpolation correcte au milieu de cellule.
///   - SampleHeight clamp : hors-bornes → renvoie la cellule de bord.
///   - Save/Load roundtrip : taille = 264244 octets, comparaison memcmp.
///   - Load rejette magic invalide.
///   - Load rejette version invalide.
///
/// Style de test aligné sur src/world_editor/world/tests/CommandStackTests.cpp
/// (REQUIRE macro maison + main() appelant chaque test function).

#include "src/client/world/terrain/TerrainChunk.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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

	using engine::world::terrain::TerrainChunk;
	using engine::world::terrain::SaveTerrainBin;
	using engine::world::terrain::LoadTerrainBin;
	using engine::world::terrain::kTerrainResolution;
	using engine::world::terrain::kTerrainCellSizeMeters;

	bool ApproxEq(float a, float b, float eps = 1e-4f)
	{
		return std::fabs(a - b) <= eps;
	}

	/// MakeFlat(2.5) → toutes les cellules à 2.5, heightMin == heightMax == 2.5.
	void Test_MakeFlat_ProducesUniformHeights()
	{
		auto chunk = TerrainChunk::MakeFlat(2.5f);
		REQUIRE(chunk.resolutionX == kTerrainResolution);
		REQUIRE(chunk.resolutionZ == kTerrainResolution);
		REQUIRE(chunk.heights.size() ==
			static_cast<size_t>(kTerrainResolution) * kTerrainResolution);
		REQUIRE(ApproxEq(chunk.heightMin, 2.5f));
		REQUIRE(ApproxEq(chunk.heightMax, 2.5f));
		bool allEqual = true;
		for (float h : chunk.heights) if (!ApproxEq(h, 2.5f)) { allEqual = false; break; }
		REQUIRE(allEqual);
	}

	/// Pose un gradient en X (h(x,z) = x), puis sample à (10.5, 0) doit retourner 10.5.
	void Test_SampleHeight_BilinearInterior()
	{
		auto chunk = TerrainChunk::MakeFlat(0.0f);
		for (uint32_t z = 0; z < chunk.resolutionZ; ++z)
			for (uint32_t x = 0; x < chunk.resolutionX; ++x)
				chunk.heights[z * chunk.resolutionX + x] = static_cast<float>(x);
		chunk.RecomputeBounds();
		REQUIRE(ApproxEq(chunk.SampleHeight(10.5f, 0.0f), 10.5f));
	}

	/// Hors bornes (positives ou négatives) → clamp.
	void Test_SampleHeight_ClampsOutOfBounds()
	{
		auto chunk = TerrainChunk::MakeFlat(7.0f);
		REQUIRE(ApproxEq(chunk.SampleHeight(1.0e6f, 1.0e6f), 7.0f));
		REQUIRE(ApproxEq(chunk.SampleHeight(-1.0e6f, -1.0e6f), 7.0f));
	}

	/// Save puis Load reproduit champ à champ + memcmp sur heights.
	void Test_SaveLoad_Roundtrip()
	{
		auto src = TerrainChunk::MakeFlat(0.0f);
		for (uint32_t z = 0; z < src.resolutionZ; ++z)
			for (uint32_t x = 0; x < src.resolutionX; ++x)
				src.heights[z * src.resolutionX + x] =
					std::sin(x * 0.1f) * std::cos(z * 0.1f);
		src.RecomputeBounds();

		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveTerrainBin(src, bytes, err));
		REQUIRE(err.empty());
		const size_t expected = 48u
			+ static_cast<size_t>(kTerrainResolution) * kTerrainResolution * sizeof(float);
		REQUIRE(bytes.size() == expected);

		TerrainChunk dst;
		REQUIRE(LoadTerrainBin(bytes, dst, err));
		REQUIRE(err.empty());
		REQUIRE(dst.resolutionX == src.resolutionX);
		REQUIRE(dst.resolutionZ == src.resolutionZ);
		REQUIRE(ApproxEq(dst.cellSizeMeters, src.cellSizeMeters));
		REQUIRE(ApproxEq(dst.heightMin, src.heightMin));
		REQUIRE(ApproxEq(dst.heightMax, src.heightMax));
		REQUIRE(std::memcmp(dst.heights.data(), src.heights.data(),
			src.heights.size() * sizeof(float)) == 0);
	}

	/// Header magic patché → Load doit échouer.
	void Test_Load_RejectsBadMagic()
	{
		std::vector<uint8_t> bytes(48u
			+ static_cast<size_t>(kTerrainResolution) * kTerrainResolution * sizeof(float), 0u);
		bytes[0] = 'X'; bytes[1] = 'X'; bytes[2] = 'X'; bytes[3] = 'X';
		TerrainChunk dst;
		std::string err;
		REQUIRE(!LoadTerrainBin(bytes, dst, err));
		REQUIRE(!err.empty());
	}

	/// Header version patché à 999 → Load doit échouer.
	void Test_Load_RejectsBadVersion()
	{
		auto src = TerrainChunk::MakeFlat(1.0f);
		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveTerrainBin(src, bytes, err));
		// Patch formatVersion (offset 4 dans le header, après magic).
		const uint32_t bogus = 999u;
		std::memcpy(bytes.data() + 4, &bogus, sizeof(uint32_t));
		// Recalcul du contentHash sur la portion post-header invalide ne corrige
		// pas la version : Load doit échouer sur la version avant le contentHash.
		TerrainChunk dst;
		REQUIRE(!LoadTerrainBin(bytes, dst, err));
	}
}

int main()
{
	Test_MakeFlat_ProducesUniformHeights();
	Test_SampleHeight_BilinearInterior();
	Test_SampleHeight_ClampsOutOfBounds();
	Test_SaveLoad_Roundtrip();
	Test_Load_RejectsBadMagic();
	Test_Load_RejectsBadVersion();

	if (g_failed == 0)
	{
		std::printf("[PASS] TerrainChunkTests (6/6)\n");
		return 0;
	}
	std::printf("[FAIL] TerrainChunkTests: %d failure(s)\n", g_failed);
	return 1;
}
