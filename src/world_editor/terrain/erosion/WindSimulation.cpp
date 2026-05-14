#include "src/world_editor/terrain/erosion/WindSimulation.h"

#include "src/world_editor/terrain/erosion/BilinearGradientSample.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace engine::editor::world::erosion
{
	WindSimulationResult RunWindSimulation(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		float seaLevelMeters,
		const WindSimulationParams& params)
	{
		WindSimulationResult result;
		if (params.numParticles == 0u || grid.width < 2 || grid.height < 2) return result;

		const auto t0 = std::chrono::steady_clock::now();

		// Direction du vent (en coords cellule, +X vers Est, +Z vers Nord).
		// Convention spec : windAngleDeg = 180° → vent venant du Nord, soufflant
		// vers le Sud → dir = (0, -1). On suit cette convention.
		const float angleRad = params.windAngleDeg * 3.14159265359f / 180.0f;
		const float windDirX = std::sin(angleRad);
		const float windDirZ = -std::cos(angleRad);
		const float exposureCells = std::max(1.0f,
			params.exposureRadiusMeters / grid.cellSizeMeters);

		std::mt19937 rng(params.rngSeed);
		std::uniform_real_distribution<float> distX(0.0f, static_cast<float>(grid.width - 1));
		std::uniform_real_distribution<float> distZ(0.0f, static_cast<float>(grid.height - 1));
		std::uniform_real_distribution<float> distJ(-0.3f, 0.3f); // petit jitter trajectoire

		uint64_t stepsAccum = 0;

		for (uint32_t p = 0; p < params.numParticles; ++p)
		{
			float posX = distX(rng);
			float posZ = distZ(rng);
			float sand = 0.0f;

			for (uint32_t step = 0; step < params.maxLifetimeSteps; ++step)
			{
				if (posX < 0.0f || posZ < 0.0f) break;
				if (posX > static_cast<float>(grid.width - 1)) break;
				if (posZ > static_cast<float>(grid.height - 1)) break;

				const HeightAndGradient hg = SampleHeightAndGradient(grid, posX, posZ);
				if (params.stopUnderSeaLevel && hg.height < seaLevelMeters)
				{
					// Dépose le sable restant à la position courante.
					if (sand > 0.0f)
					{
						DistributeBilinearDelta(grid, posX, posZ,
							std::min(sand, params.maxDeltaPerCellMeters),
							result.deltas);
						result.cellsDeposited++;
					}
					break;
				}

				// Calcule l'exposition : différence de hauteur vs upwind.
				const float upX = posX - windDirX * exposureCells;
				const float upZ = posZ - windDirZ * exposureCells;
				const HeightAndGradient hgUp = SampleHeightAndGradient(grid, upX, upZ);
				const float exposure = hg.height - hgUp.height;

				const float capacity = std::max(0.0f, exposure)
					* params.windStrength * params.sandCapacityFactor;

				if (sand > capacity)
				{
					const float amount = (sand - capacity) * params.depositionRate;
					const float clamped = std::clamp(amount, 0.0f,
						params.maxDeltaPerCellMeters);
					DistributeBilinearDelta(grid, posX, posZ, +clamped, result.deltas);
					sand -= clamped;
					if (clamped > 0.0f) result.cellsDeposited++;
				}
				else
				{
					float amount = (capacity - sand) * params.erosionRate;
					// Cap à 5 % de la hauteur locale pour éviter de creuser
					// jusque sous le sea level.
					amount = std::min(amount, std::max(0.0f, hg.height * 0.05f));
					// restrictToSandSplat non câblé dans cette MVP (cf. CODEBASE_MAP) :
					// l'érosion s'applique uniformément à la heightmap.
					(void)params.restrictToSandSplat;
					(void)params.sandSplatLayerIndex;
					const float clamped = std::clamp(amount, 0.0f,
						params.maxDeltaPerCellMeters);
					DistributeBilinearDelta(grid, posX, posZ, -clamped, result.deltas);
					sand += clamped;
					if (clamped > 0.0f) result.cellsEroded++;
				}

				// Avance dans la direction du vent + petit jitter.
				posX += windDirX + distJ(rng);
				posZ += windDirZ + distJ(rng);
				stepsAccum++;
			}
			result.particlesSimulated++;
		}
		result.totalSteps = stepsAccum;

		// Clamp final + min/max.
		for (auto& kv : result.deltas)
		{
			for (auto& cell : kv.second)
			{
				cell.second = std::clamp(cell.second,
					-params.maxDeltaPerCellMeters,
					+params.maxDeltaPerCellMeters);
				result.minDelta = std::min(result.minDelta, cell.second);
				result.maxDelta = std::max(result.maxDelta, cell.second);
			}
		}

		const auto t1 = std::chrono::steady_clock::now();
		result.wallTimeMillis = static_cast<double>(
			std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()) / 1000.0;
		return result;
	}
}
