#include "src/world_editor/terrain/erosion/ThermalSimulation.h"

#include "src/client/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

namespace engine::editor::world::erosion
{
	namespace
	{
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);

		struct Neighbor { int dx; int dz; };
		constexpr Neighbor kNeighbors[8] = {
			{+1, +1}, {+1,  0}, {+1, -1}, { 0, -1},
			{-1, -1}, {-1,  0}, {-1, +1}, { 0, +1},
		};

		/// Émission d'un delta pour la cellule globale `(globalCellX, globalCellZ)`
		/// avec gestion des frontières inter-chunks (cellule pile sur le bord
		/// émise vers tous les chunks adjacents). Identique au pattern
		/// `BilinearGradientSample::EmitAt` mais sans bilinéaire (cellule
		/// entière uniquement).
		void EmitDelta(int globalCellX, int globalCellZ, float delta,
			engine::editor::world::SparseChunkDeltas& outDeltas)
		{
			if (delta == 0.0f) return;
			const int step = kRes - 1;
			const int chunkX = globalCellX / step;
			const int chunkZ = globalCellZ / step;
			const int localX = globalCellX - chunkX * step;
			const int localZ = globalCellZ - chunkZ * step;

			auto pushOne = [&](int cx, int cz, int lx, int lz)
			{
				const engine::world::GlobalChunkCoord coord{ cx, cz };
				const uint32_t idx = static_cast<uint32_t>(lz * kRes + lx);
				outDeltas[coord][idx] += delta;
			};

			pushOne(chunkX, chunkZ, localX, localZ);

			const bool onWest  = (localX == 0     && chunkX > 0);
			const bool onEast  = (localX == step);
			const bool onSouth = (localZ == 0     && chunkZ > 0);
			const bool onNorth = (localZ == step);

			if (onWest)  pushOne(chunkX - 1, chunkZ, step, localZ);
			if (onEast)  pushOne(chunkX + 1, chunkZ, 0,    localZ);
			if (onSouth) pushOne(chunkX, chunkZ - 1, localX, step);
			if (onNorth) pushOne(chunkX, chunkZ + 1, localX, 0);
		}
	}

	ThermalSimulationResult RunThermalSimulation(
		engine::editor::world::ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		const ThermalSimulationParams& params)
	{
		ThermalSimulationResult result;
		if (grid.width < 2 || grid.height < 2) return result;

		const auto t0 = std::chrono::steady_clock::now();

		const float talusSlope =
			std::tan(params.talusAngleDeg * 3.14159265359f / 180.0f);
		const float minActSlope =
			std::tan(params.minActivationSlopeDeg * 3.14159265359f / 180.0f);
		const float preserveSlope = std::tan(
			params.preserveSteepThresholdDeg * 3.14159265359f / 180.0f);
		const float convergenceThreshold =
			params.convergenceThresholdMetersPerCell *
			static_cast<float>(grid.width) * static_cast<float>(grid.height);

		// Pré-calcul des distances pour chaque voisin.
		float distMeters[8];
		for (int i = 0; i < 8; ++i)
		{
			const float dx = static_cast<float>(kNeighbors[i].dx);
			const float dz = static_cast<float>(kNeighbors[i].dz);
			distMeters[i] = std::sqrt(dx * dx + dz * dz) * grid.cellSizeMeters;
		}

		std::vector<float> next = grid.heights;
		for (uint32_t pass = 0; pass < params.numPasses; ++pass)
		{
			float totalTransfer = 0.0f;
			uint32_t cellsTouched = 0;

			// Pour chaque cellule : calcule les excès vers les voisins descendants.
			for (int z = 0; z < grid.height; ++z)
			{
				for (int x = 0; x < grid.width; ++x)
				{
					const float h = grid.Get(x, z);
					if (params.stopUnderSeaLevel && h <= seaLevelMeters) continue;

					float excessTotal = 0.0f;
					float perNeighborExcess[8] = { 0.0f };
					for (int n = 0; n < 8; ++n)
					{
						const int nx = x + kNeighbors[n].dx;
						const int nz = z + kNeighbors[n].dz;
						if (nx < 0 || nx >= grid.width || nz < 0 || nz >= grid.height) continue;
						const float h_n = grid.Get(nx, nz);
						const float slope = (h - h_n) / distMeters[n];
						if (slope <= minActSlope) continue;
						if (params.preserveSteepSlopes && slope > preserveSlope) continue;
						if (slope <= talusSlope) continue;

						const float excess = (slope - talusSlope) * distMeters[n]
							* params.forcePerPass * 0.5f;
						if (excess <= 0.0f) continue;
						perNeighborExcess[n] = excess;
						excessTotal += excess;
					}

					if (excessTotal <= 0.0f) continue;
					// Anti-runaway : on ne peut pas relâcher plus que la
					// hauteur disponible au-dessus du sea level (ou 0 par
					// défaut). Borne soft : `min(1, h / excessTotal)` pour
					// préserver la masse.
					const float factor = std::min(1.0f, h / std::max(excessTotal, 1e-6f));
					for (int n = 0; n < 8; ++n)
					{
						const float e = perNeighborExcess[n];
						if (e <= 0.0f) continue;
						const float delta = e * factor;
						const int nx = x + kNeighbors[n].dx;
						const int nz = z + kNeighbors[n].dz;
						const int gxSelf = grid.originCellX + x;
						const int gzSelf = grid.originCellZ + z;
						const int gxN    = grid.originCellX + nx;
						const int gzN    = grid.originCellZ + nz;
						EmitDelta(gxSelf, gzSelf, -delta, result.deltas);
						EmitDelta(gxN,    gzN,    +delta, result.deltas);
						// Mute la copie locale pour les passes suivantes.
						next[static_cast<size_t>(z) * grid.width + x] -= delta;
						next[static_cast<size_t>(nz) * grid.width + nx] += delta;
						totalTransfer += delta;
					}
					cellsTouched++;
				}
			}

			grid.heights = next;
			result.totalTransferredMeters += totalTransfer;
			result.cellsAffected = std::max(result.cellsAffected, cellsTouched);
			result.passesExecuted = pass + 1u;
			if (totalTransfer < convergenceThreshold)
			{
				result.converged = true;
				break;
			}
		}

		const auto t1 = std::chrono::steady_clock::now();
		result.wallTimeMillis = static_cast<double>(
			std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) / 1000.0;
		return result;
	}
}
