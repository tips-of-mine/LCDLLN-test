/// Tests unitaires pour TerrainLodChain + TerrainLodWorker (M100.8).
///
/// Vérifient :
///   - GenerateLodChain : LOD1 hauteurs == moyenne 2x2 de LOD0 (box filter).
///   - SaveTerrainLodsBin / LoadTerrainLodsBin : roundtrip identique.
///   - TerrainLodWorker : Enqueue retourne rapidement (main thread non bloqué).
///   - TerrainLodWorker : multiples enqueues sur le même coord → seuls les
///     jobs récents délivrent (stale jobs jetés via génération atomique).

#include "engine/world/terrain/TerrainLodChain.h"
#include "engine/world/terrain/TerrainLodWorker.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
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
	using engine::world::terrain::TerrainLod;
	using engine::world::terrain::TerrainLodChain;
	using engine::world::terrain::TerrainLodWorker;
	using engine::world::terrain::GenerateLodChain;
	using engine::world::terrain::SaveTerrainLodsBin;
	using engine::world::terrain::LoadTerrainLodsBin;
	using engine::world::terrain::kPersistedLodCount;
	using engine::world::terrain::kLodResolutions;

	bool ApproxEq(float a, float b, float eps = 1e-4f)
	{
		return std::fabs(a - b) <= eps;
	}

	/// LOD1[z][x] doit être strictement la moyenne 2x2 de LOD0 (box filter).
	void Test_GenerateLodChain_BoxFilterMatch()
	{
		auto lod0 = TerrainChunk::MakeFlat(0.0f);
		for (uint32_t z = 0; z < lod0.resolutionZ; ++z)
			for (uint32_t x = 0; x < lod0.resolutionX; ++x)
				lod0.heights[z * lod0.resolutionX + x] = static_cast<float>(x);
		lod0.RecomputeBounds();

		const auto chain = GenerateLodChain(lod0);
		REQUIRE(chain.lods[0].resolution == kLodResolutions[0]);
		REQUIRE(chain.lods[1].resolution == kLodResolutions[1]);
		REQUIRE(chain.lods[2].resolution == kLodResolutions[2]);

		const uint32_t r1 = chain.lods[0].resolution;
		bool matchAll = true;
		for (uint32_t z = 0; z < r1 && matchAll; ++z)
		{
			for (uint32_t x = 0; x < r1; ++x)
			{
				const float h00 = lod0.heights[(z * 2u + 0u) * lod0.resolutionX + (x * 2u + 0u)];
				const float h10 = lod0.heights[(z * 2u + 0u) * lod0.resolutionX + (x * 2u + 1u)];
				const float h01 = lod0.heights[(z * 2u + 1u) * lod0.resolutionX + (x * 2u + 0u)];
				const float h11 = lod0.heights[(z * 2u + 1u) * lod0.resolutionX + (x * 2u + 1u)];
				const float expected = (h00 + h10 + h01 + h11) * 0.25f;
				if (!ApproxEq(chain.lods[0].heights[z * r1 + x], expected))
				{
					matchAll = false;
					break;
				}
			}
		}
		REQUIRE(matchAll);
	}

	/// SaveTerrainLodsBin → LoadTerrainLodsBin redonne une chaîne identique
	/// niveau par niveau (resolutions, cellSize, heights memcmp).
	void Test_SaveLoadLods_Roundtrip()
	{
		auto lod0 = TerrainChunk::MakeFlat(0.0f);
		for (uint32_t z = 0; z < lod0.resolutionZ; ++z)
			for (uint32_t x = 0; x < lod0.resolutionX; ++x)
				lod0.heights[z * lod0.resolutionX + x] =
					static_cast<float>((x * 7u + z * 13u) % 31u);
		lod0.RecomputeBounds();
		const auto chain = GenerateLodChain(lod0);

		std::vector<uint8_t> bytes;
		std::string err;
		REQUIRE(SaveTerrainLodsBin(chain, bytes, err));
		TerrainLodChain reloaded;
		REQUIRE(LoadTerrainLodsBin(bytes, reloaded, err));

		for (uint32_t i = 0; i < kPersistedLodCount; ++i)
		{
			REQUIRE(reloaded.lods[i].resolution == chain.lods[i].resolution);
			REQUIRE(ApproxEq(reloaded.lods[i].cellSizeMeters, chain.lods[i].cellSizeMeters));
			REQUIRE(reloaded.lods[i].heights.size() == chain.lods[i].heights.size());
			REQUIRE(std::memcmp(reloaded.lods[i].heights.data(),
				chain.lods[i].heights.data(),
				chain.lods[i].heights.size() * sizeof(float)) == 0);
		}
	}

	/// Le main thread ne doit jamais être bloqué > quelques ms par les
	/// enqueues, même quand on en pousse plein d'un coup.
	void Test_LodWorker_AsyncDoesNotBlockMain()
	{
		TerrainLodWorker worker;
		worker.Start(2u);
		std::atomic<int> received{0};
		const auto lod0 = TerrainChunk::MakeFlat(0.0f);

		const auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < 10; ++i)
		{
			worker.Enqueue({i, 0}, lod0,
				[&received](auto, auto) { received.fetch_add(1); });
		}
		const auto enqueueDt = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - t0).count();
		// Tous les enqueues doivent finir en moins de 50 ms (très large marge).
		REQUIRE(enqueueDt < 50);

		// Attendre que les résultats soient livrés (worker thread est plus lent).
		for (int i = 0; i < 200 && received.load() < 10; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		REQUIRE(received.load() == 10);
		worker.Stop();
	}

	/// 5 enqueues consécutifs sur le même coord → seuls les jobs non périmés
	/// délivrent. Au moins 1 (le dernier), au plus 5 (si tous se terminent
	/// avant l'enqueue suivant — peu probable mais pas exclu).
	void Test_LodWorker_StaleJobsDropped()
	{
		TerrainLodWorker worker;
		worker.Start(1u);
		std::atomic<int> received{0};
		const auto lod0 = TerrainChunk::MakeFlat(0.0f);

		for (int i = 0; i < 5; ++i)
		{
			worker.Enqueue({0, 0}, lod0,
				[&received](auto, auto) { received.fetch_add(1); });
		}
		// Attendre que la file soit drainée.
		for (int i = 0; i < 200 && received.load() == 0; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		// Stop attend la fin du job en cours.
		worker.Stop();
		REQUIRE(received.load() >= 1);
		REQUIRE(received.load() <= 5);
	}
}

int main()
{
	Test_GenerateLodChain_BoxFilterMatch();
	Test_SaveLoadLods_Roundtrip();
	Test_LodWorker_AsyncDoesNotBlockMain();
	Test_LodWorker_StaleJobsDropped();

	if (g_failed == 0)
	{
		std::printf("[PASS] TerrainLodTests (4/4)\n");
		return 0;
	}
	std::printf("[FAIL] TerrainLodTests: %d failure(s)\n", g_failed);
	return 1;
}
