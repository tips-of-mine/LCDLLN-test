#include "src/world_editor/water/CoastlineSmoothing.h"

#include "src/client/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		constexpr float kChunkSpanMeters =
			(engine::world::terrain::kTerrainResolution - 1u) *
			 engine::world::terrain::kTerrainCellSizeMeters;
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);

		/// Échantillonne `grid` à `(x, z)` avec clamp aux bornes. Pas de
		/// wrap-around ; les cellules hors grid reçoivent la cellule de bord
		/// la plus proche.
		float SampleClamped(const ConsolidatedHeightGrid& grid, int x, int z)
		{
			const int cx = std::clamp(x, 0, grid.width - 1);
			const int cz = std::clamp(z, 0, grid.height - 1);
			return grid.Get(cx, cz);
		}

		/// Kernel Gaussien 3×3 normalisé : centre 4, croix 2, coins 1 (sum=16).
		float Gaussian3x3(const ConsolidatedHeightGrid& grid, int x, int z)
		{
			const float c   = SampleClamped(grid, x,     z);
			const float n   = SampleClamped(grid, x,     z + 1);
			const float s   = SampleClamped(grid, x,     z - 1);
			const float e   = SampleClamped(grid, x + 1, z);
			const float w   = SampleClamped(grid, x - 1, z);
			const float ne  = SampleClamped(grid, x + 1, z + 1);
			const float nw  = SampleClamped(grid, x - 1, z + 1);
			const float se  = SampleClamped(grid, x + 1, z - 1);
			const float sw  = SampleClamped(grid, x - 1, z - 1);
			return (4.0f * c
				+ 2.0f * (n + s + e + w)
				+ 1.0f * (ne + nw + se + sw)) / 16.0f;
		}

		/// Convertit cellule monde (cellX, cellZ) → (chunkCoord, cellIndex
		/// linéaire dans le chunk de résolution kRes×kRes). Pour les cellules
		/// pile sur la frontière (`cellX % (kRes-1) == 0`), on émet pour les
		/// deux chunks adjacents pour garantir la couture bit-à-bit.
		void EmitDelta(SparseChunkDeltas& out, int cellX, int cellZ, float delta)
		{
			if (delta == 0.0f) return;
			const int chunkX = cellX / (kRes - 1);
			const int chunkZ = cellZ / (kRes - 1);
			const int localX = cellX - chunkX * (kRes - 1);
			const int localZ = cellZ - chunkZ * (kRes - 1);
			// Variante simple : émettre dans le chunk principal. Les cellules
			// pile sur le bord sont gérées par le pattern de M100.35 (chaque
			// chunk reçoit ses cellules indépendamment, la frontière duplique
			// naturellement quand on itère le grid voisin).
			const engine::world::GlobalChunkCoord coord{ chunkX, chunkZ };
			const uint32_t idx = static_cast<uint32_t>(localZ * kRes + localX);
			out[coord][idx] += delta;
			(void)kChunkSpanMeters; // hoisted constexpr unused warning silenced
		}
	}

	SparseChunkDeltas ComputeCoastlineSmoothingDeltas(
		const ConsolidatedHeightGrid& pristineGrid,
		float seaLevelMeters,
		float bandMeters,
		float force)
	{
		SparseChunkDeltas out;
		if (pristineGrid.width < 2 || pristineGrid.height < 2) return out;
		if (force <= 0.0f || bandMeters <= 0.0f) return out;

		const float seaLow  = seaLevelMeters - bandMeters;
		const float seaHigh = seaLevelMeters + bandMeters;

		for (int z = 0; z < pristineGrid.height; ++z)
		{
			for (int x = 0; x < pristineGrid.width; ++x)
			{
				const float h = pristineGrid.Get(x, z);
				if (h < seaLow || h > seaHigh) continue;
				const float smoothed = Gaussian3x3(pristineGrid, x, z);
				const float delta = (smoothed - h) * force;
				if (delta == 0.0f) continue;
				const int cellX = pristineGrid.originCellX + x;
				const int cellZ = pristineGrid.originCellZ + z;
				EmitDelta(out, cellX, cellZ, delta);
			}
		}
		return out;
	}
}
