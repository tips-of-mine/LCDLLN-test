/// Tests de parité éditeur ↔ client pour le format `terrain.bin` (M100.5).
///
/// Vérifient que :
///   1. Un `TerrainChunk` écrit via `SaveTerrainBin` et relu via
///      `LoadTerrainBin` produit une struct identique champ à champ +
///      memcmp byte-exact sur `heights`.
///   2. Le mesh CPU produit par `BuildLod0Mesh` à partir d'un chunk source
///      et d'un chunk re-lu est identique bit-à-bit (vertex + index buffers).
///
/// Ces tests sont la garantie technique du contrat « éditeur écrit, client
/// lit » de la spec M100.5. Ils n'utilisent ni Vulkan ni ImGui.

#include "src/client/world/terrain/TerrainChunk.h"
#include "src/client/world/terrain/TerrainMeshBuilder.h"

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
	using engine::world::terrain::TerrainMeshCpu;
	using engine::world::terrain::TerrainVertex;
	using engine::world::terrain::SaveTerrainBin;
	using engine::world::terrain::LoadTerrainBin;
	using engine::world::terrain::BuildLod0Mesh;
	using engine::world::terrain::kTerrainResolution;

	bool ApproxEq(float a, float b, float eps = 1e-4f)
	{
		return std::fabs(a - b) <= eps;
	}

	/// Écrit un chunk avec un pattern déterministe, le relit, compare champ à
	/// champ + memcmp sur heights : identique à l'octet près.
	void Test_EditorWritesClientReadsIdentical()
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

		TerrainChunk dst;
		REQUIRE(LoadTerrainBin(bytes, dst, err));

		REQUIRE(dst.resolutionX == src.resolutionX);
		REQUIRE(dst.resolutionZ == src.resolutionZ);
		REQUIRE(ApproxEq(dst.cellSizeMeters, src.cellSizeMeters));
		REQUIRE(ApproxEq(dst.heightMin, src.heightMin));
		REQUIRE(ApproxEq(dst.heightMax, src.heightMax));
		REQUIRE(std::memcmp(dst.heights.data(), src.heights.data(),
			src.heights.size() * sizeof(float)) == 0);
	}

	/// Construit deux meshes (depuis chunk source + chunk re-lu), compare
	/// vertex et index buffers byte-exact. Garantit qu'aucune divergence ne
	/// s'introduit entre l'écriture éditeur et la consommation client.
	void Test_MeshBuilder_DeterministicVertexBuffer()
	{
		auto src = TerrainChunk::MakeFlat(0.0f);
		for (uint32_t z = 0; z < src.resolutionZ; ++z)
			for (uint32_t x = 0; x < src.resolutionX; ++x)
				src.heights[z * src.resolutionX + x] = static_cast<float>(x + z);
		src.RecomputeBounds();

		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveTerrainBin(src, bytes, err));
		TerrainChunk reloaded;
		REQUIRE(LoadTerrainBin(bytes, reloaded, err));

		const auto m1 = BuildLod0Mesh(src);
		const auto m2 = BuildLod0Mesh(reloaded);
		REQUIRE(m1.vertices.size() == m2.vertices.size());
		REQUIRE(std::memcmp(m1.vertices.data(), m2.vertices.data(),
			m1.vertices.size() * sizeof(TerrainVertex)) == 0);
		REQUIRE(m1.indices.size() == m2.indices.size());
		REQUIRE(std::memcmp(m1.indices.data(), m2.indices.data(),
			m1.indices.size() * sizeof(uint32_t)) == 0);
	}
}

int main()
{
	Test_EditorWritesClientReadsIdentical();
	Test_MeshBuilder_DeterministicVertexBuffer();

	if (g_failed == 0)
	{
		std::printf("[PASS] TerrainParityTests (2/2)\n");
		return 0;
	}
	std::printf("[FAIL] TerrainParityTests: %d failure(s)\n", g_failed);
	return 1;
}
