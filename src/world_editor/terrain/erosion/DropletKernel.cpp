#include "src/world_editor/terrain/erosion/DropletKernel.h"

#include "src/world_editor/terrain/erosion/BilinearGradientSample.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world::erosion
{
	namespace
	{
		/// Normalise un vecteur 2D, retourne (0, 0) si la norme est nulle
		/// pour éviter NaN.
		inline void Normalize2D(float& x, float& z)
		{
			const float n = std::sqrt(x * x + z * z);
			if (n < 1e-8f) { x = 0.0f; z = 0.0f; return; }
			x /= n; z /= n;
		}

		/// Pente locale en degrés (depuis gradient cell-relatif).
		float SlopeDegFromGradient(float gx, float gz)
		{
			const float mag = std::sqrt(gx * gx + gz * gz);
			return std::atan(mag) * (180.0f / 3.14159265359f);
		}
	}

	DropletKernelResult RunSingleDroplet(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		const HydraulicSimulationParams& params,
		float seaLevelMeters,
		float startCellX, float startCellZ,
		engine::editor::world::SparseChunkDeltas& outDeltas)
	{
		DropletKernelResult stats;
		float posX = startCellX;
		float posZ = startCellZ;
		float velX = 0.0f, velZ = 0.0f;
		float water = 1.0f;
		float sediment = 0.0f;

		for (uint32_t step = 0; step < params.maxLifetimeSteps; ++step)
		{
			const HeightAndGradient hg0 = SampleHeightAndGradient(grid, posX, posZ);

			// Préservation des plats : si la pente est sous le seuil, on
			// stoppe la goutte (pas d'érosion erratique sur plat).
			if (params.preserveFlatAreas)
			{
				const float slopeDeg = SlopeDegFromGradient(hg0.gradientX, hg0.gradientZ);
				if (slopeDeg < params.flatAreaSlopeThresholdDeg) break;
			}

			// Mise à jour de la vélocité (gravité dans le sens descendant
			// du gradient — un gradient positif en X signifie h augmente
			// avec X, donc l'eau va dans -X).
			velX = velX * (1.0f - params.inertia) - hg0.gradientX * params.gravity;
			velZ = velZ * (1.0f - params.inertia) - hg0.gradientZ * params.gravity;

			float dx = velX;
			float dz = velZ;
			Normalize2D(dx, dz);
			if (dx == 0.0f && dz == 0.0f) break;  // velocity nulle = stuck

			const float newX = posX + dx;
			const float newZ = posZ + dz;
			if (newX < 0.0f || newZ < 0.0f) break;
			if (newX > static_cast<float>(grid.width - 1)) break;
			if (newZ > static_cast<float>(grid.height - 1)) break;

			const HeightAndGradient hg1 = SampleHeightAndGradient(grid, newX, newZ);
			const float deltaH = hg1.height - hg0.height;

			// Stop sous le niveau de mer.
			if (params.stopUnderSeaLevel && hg1.height <= seaLevelMeters)
			{
				// Dépose le sédiment restant à la position actuelle.
				if (sediment > 0.0f)
				{
					DistributeBilinearDelta(grid, posX, posZ,
						std::min(sediment, params.maxDeltaPerCellMeters),
						outDeltas);
					stats.cellsDeposited++;
				}
				break;
			}

			const float velMag = std::sqrt(velX * velX + velZ * velZ);
			// Capacité : seuil min sur la pente positive descendante
			// (-deltaH) pour activer l'érosion. Si la goutte monte, la
			// capacité chute à minSlope (le sédiment se dépose).
			const float capacity = std::max(-deltaH, params.minSlopeForErosion)
				* velMag * water * params.sedimentCapacity;

			if (sediment > capacity)
			{
				// Déposition.
				const float amountDeposit = (sediment - capacity) * params.depositionRate;
				const float clamped = std::clamp(amountDeposit, 0.0f,
					params.maxDeltaPerCellMeters);
				DistributeBilinearDelta(grid, posX, posZ, +clamped, outDeltas);
				sediment -= clamped;
				if (clamped > 0.0f) stats.cellsDeposited++;
			}
			else
			{
				// Érosion : on ne peut pas creuser plus que la moitié de
				// la dénivelée descendante (évite les pits artificiels).
				const float maxErode = std::max(0.0f, -deltaH * 0.5f);
				const float amountErode = std::min(
					(capacity - sediment) * params.erosionRate, maxErode);
				const float clamped = std::clamp(amountErode, 0.0f,
					params.maxDeltaPerCellMeters);
				DistributeBilinearDelta(grid, posX, posZ, -clamped, outDeltas);
				sediment += clamped;
				if (clamped > 0.0f) stats.cellsEroded++;
			}

			water *= (1.0f - params.evaporationRate);
			if (water < 1e-4f) break;

			posX = newX;
			posZ = newZ;
			stats.stepsTaken++;
		}
		return stats;
	}
}
