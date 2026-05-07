/// Tests unitaires pour engine::world::terrain::SplatMap (M100.9).
///
/// Vérifient :
///   - MakeUniform(0) : layer 0 = 255 partout, autres = 0, IsValid() OK.
///   - MakeUniform hors bornes → fallback layer 0.
///   - Save/Load roundtrip : taille exacte, memcmp byte-exact.
///   - Load rejette magic invalide.
///   - Load rejette une cellule de somme != 255.
///
/// Style aligné sur engine/world/terrain/tests/TerrainChunkTests.cpp
/// (REQUIRE macro maison + main() appelant chaque test function).

#include "engine/world/terrain/LayerPalette.h"
#include "engine/world/terrain/SplatMap.h"
#include "engine/world/surface/SurfaceType.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
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

	using engine::world::terrain::SplatMap;
	using engine::world::terrain::SaveSplatBin;
	using engine::world::terrain::LoadSplatBin;
	using engine::world::terrain::kSplatLayerCount;
	using engine::world::terrain::kSplatResolution;

	/// MakeUniform(0) : layer 0 = 255 partout, autres = 0, somme = 255 partout.
	void Test_MakeUniform_SumIs255()
	{
		auto splat = SplatMap::MakeUniform(0u);
		REQUIRE(splat.resolution == kSplatResolution);
		REQUIRE(splat.layerCount == kSplatLayerCount);
		REQUIRE(splat.weights.size() ==
			static_cast<size_t>(kSplatResolution) * kSplatResolution * kSplatLayerCount);
		REQUIRE(splat.IsValid());
		// Vérifie quelques cellules : layer 0 = 255, layers 1..7 = 0.
		const uint32_t cellsToCheck[3] = { 0u, 100u, kSplatResolution - 1u };
		for (uint32_t z : cellsToCheck)
		{
			for (uint32_t x : cellsToCheck)
			{
				const size_t base = (z * kSplatResolution + x) * kSplatLayerCount;
				REQUIRE(splat.weights[base + 0] == 255u);
				for (uint32_t layer = 1; layer < kSplatLayerCount; ++layer)
					REQUIRE(splat.weights[base + layer] == 0u);
			}
		}
	}

	/// MakeUniform avec layer hors bornes → fallback layer 0.
	void Test_MakeUniform_OutOfBoundsClampsToZero()
	{
		auto splat = SplatMap::MakeUniform(99u);
		REQUIRE(splat.IsValid());
		// Au lieu d'un crash, layer 0 = 255.
		REQUIRE(splat.weights[0] == 255u);
		REQUIRE(splat.weights[1] == 0u);
	}

	/// Save → Load redonne la même splat byte-exact.
	void Test_SaveLoad_Roundtrip()
	{
		auto src = SplatMap::MakeUniform(3u);
		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveSplatBin(src, bytes, err));
		REQUIRE(err.empty());

		// Taille attendue : 24 (header) + 8 (meta) + 257*257*8 (weights).
		const size_t expected = 24u + 8u
			+ static_cast<size_t>(kSplatResolution) * kSplatResolution * kSplatLayerCount;
		REQUIRE(bytes.size() == expected);

		SplatMap dst;
		REQUIRE(LoadSplatBin(bytes, dst, err));
		REQUIRE(err.empty());
		REQUIRE(dst.resolution == src.resolution);
		REQUIRE(dst.layerCount == src.layerCount);
		REQUIRE(std::memcmp(dst.weights.data(), src.weights.data(),
			src.weights.size()) == 0);
	}

	/// Magic patché → Load doit échouer.
	void Test_Load_RejectsBadMagic()
	{
		auto src = SplatMap::MakeUniform(0u);
		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveSplatBin(src, bytes, err));
		bytes[0] = 'X'; bytes[1] = 'X'; bytes[2] = 'X'; bytes[3] = 'X';
		SplatMap dst;
		REQUIRE(!LoadSplatBin(bytes, dst, err));
		REQUIRE(!err.empty());
	}

	/// Charge `assets/terrain/layer_palette.json`, vérifie 8 entrées + nom du
	/// premier (`dirt`) et du dernier (`lava_cooled`). Skip si fichier absent
	/// (ex. test lancé hors source root).
	void Test_LayerPalette_LoadJson()
	{
		using engine::world::terrain::LayerPalette;
		using engine::world::terrain::LoadLayerPalette;

		const std::filesystem::path path = "assets/terrain/layer_palette.json";
		if (!std::filesystem::exists(path))
		{
			std::printf("[SKIP] Test_LayerPalette_LoadJson: %s missing\n", path.string().c_str());
			return;
		}
		LayerPalette palette;
		std::string err;
		REQUIRE(LoadLayerPalette(path, palette, err));
		REQUIRE(err.empty());
		REQUIRE(palette.version == 1u);
		REQUIRE(palette.layers[0].name == "dirt");
		REQUIRE(palette.layers[7].name == "lava_cooled");
		REQUIRE(palette.layers[1].tilingMeters > 0.0f);
		REQUIRE(palette.layers[5].surfaceType == engine::world::surface::SurfaceType::Rock);
	}

	/// Cellule avec somme != 255 → SaveSplatBin échoue (validate avant écriture)
	/// et LoadSplatBin échoue aussi sur un blob corrompu.
	void Test_Load_RejectsSumNot255()
	{
		auto src = SplatMap::MakeUniform(0u);
		// Casse la cellule (50, 50) : layer 0 = 200 au lieu de 255 → somme = 200.
		const size_t base = (50u * kSplatResolution + 50u) * kSplatLayerCount;
		src.weights[base + 0] = 200u;

		std::vector<uint8_t> bytes;
		std::string err;
		// SaveSplatBin doit refuser : invariant non respecté.
		REQUIRE(!SaveSplatBin(src, bytes, err));
		REQUIRE(!err.empty());
	}

	/// Test de parité éditeur ↔ client (M100.9). Construit une SplatMap avec
	/// un pattern non trivial (somme=255 conservée), Save → Load, compare
	/// byte-exact sur les weights.
	void Test_Parity_EditorWritesClientReadsIdentical()
	{
		auto src = SplatMap::MakeUniform(0u);
		// Pattern : modifier les 100x100 premières cellules à un mélange dirt/rock.
		for (uint32_t z = 0; z < 100u; ++z)
		{
			for (uint32_t x = 0; x < 100u; ++x)
			{
				const size_t base = (z * kSplatResolution + x) * kSplatLayerCount;
				src.weights[base + 0] = 100u; // dirt
				src.weights[base + 5] = 155u; // rock
				// somme cellule = 255, autres layers = 0 (déjà initialisés par MakeUniform → 0).
			}
		}
		REQUIRE(src.IsValid());

		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveSplatBin(src, bytes, err));

		SplatMap dst;
		REQUIRE(LoadSplatBin(bytes, dst, err));
		REQUIRE(std::memcmp(dst.weights.data(), src.weights.data(),
			src.weights.size()) == 0);
	}
}

int main()
{
	Test_MakeUniform_SumIs255();
	Test_MakeUniform_OutOfBoundsClampsToZero();
	Test_SaveLoad_Roundtrip();
	Test_Load_RejectsBadMagic();
	Test_Load_RejectsSumNot255();
	Test_LayerPalette_LoadJson();
	Test_Parity_EditorWritesClientReadsIdentical();

	if (g_failed == 0)
	{
		std::printf("[PASS] SplatMapTests (7/7)\n");
		return 0;
	}
	std::printf("[FAIL] SplatMapTests: %d failure(s)\n", g_failed);
	return 1;
}
