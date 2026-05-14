#include "src/world_editor/terrain/erosion/BilinearGradientSample.h"

#include "src/client/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world::erosion
{
	namespace
	{
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);

		/// Lecture clamped (out-of-bounds → cellule de bord).
		float SampleClamped(const engine::editor::world::ConsolidatedHeightGrid& g,
			int x, int z)
		{
			const int cx = std::clamp(x, 0, g.width - 1);
			const int cz = std::clamp(z, 0, g.height - 1);
			return g.Get(cx, cz);
		}
	}

	HeightAndGradient SampleHeightAndGradient(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		float posCellX, float posCellZ)
	{
		HeightAndGradient out{ 0.0f, 0.0f, 0.0f };
		if (posCellX < 0.0f || posCellZ < 0.0f) return out;
		if (posCellX > static_cast<float>(grid.width - 1) ||
			posCellZ > static_cast<float>(grid.height - 1)) return out;

		const int x0 = static_cast<int>(std::floor(posCellX));
		const int z0 = static_cast<int>(std::floor(posCellZ));
		const int x1 = std::min(x0 + 1, grid.width - 1);
		const int z1 = std::min(z0 + 1, grid.height - 1);
		const float tx = posCellX - static_cast<float>(x0);
		const float tz = posCellZ - static_cast<float>(z0);

		const float h00 = SampleClamped(grid, x0, z0);
		const float h10 = SampleClamped(grid, x1, z0);
		const float h01 = SampleClamped(grid, x0, z1);
		const float h11 = SampleClamped(grid, x1, z1);

		// Bilinéaire pour la hauteur.
		const float hz0 = h00 + (h10 - h00) * tx;
		const float hz1 = h01 + (h11 - h01) * tx;
		out.height = hz0 + (hz1 - hz0) * tz;

		// Gradient par interpolation des dérivées finies aux coins.
		// ∂h/∂x au point Z0 : (h10 - h00). À z1 : (h11 - h01).
		const float gx_z0 = h10 - h00;
		const float gx_z1 = h11 - h01;
		out.gradientX = gx_z0 + (gx_z1 - gx_z0) * tz;
		// ∂h/∂z au point X0 : (h01 - h00). À x1 : (h11 - h10).
		const float gz_x0 = h01 - h00;
		const float gz_x1 = h11 - h10;
		out.gradientZ = gz_x0 + (gz_x1 - gz_x0) * tx;

		return out;
	}

	namespace
	{
		/// Convertit une cellule globale (gridCellX = originCellX + localX) en
		/// (chunkCoord, cellIndex local au chunk). Pour les cellules pile sur
		/// la frontière (`cellX % (kRes - 1) == 0`), on émet dans le chunk
		/// "à gauche" en utilisant `localX = kRes - 1` ET dans le chunk "à
		/// droite" avec `localX = 0`. Idem en Z. Couture préservée bit-à-bit.
		void EmitAt(int globalCellX, int globalCellZ, float delta,
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

			const bool onWest  = (localX == 0          && chunkX > 0);
			const bool onEast  = (localX == step       /* tjs valide */);
			const bool onSouth = (localZ == 0          && chunkZ > 0);
			const bool onNorth = (localZ == step       /* tjs valide */);

			if (onWest)  pushOne(chunkX - 1, chunkZ, step, localZ);
			if (onEast)  pushOne(chunkX + 1, chunkZ, 0,    localZ);
			if (onSouth) pushOne(chunkX, chunkZ - 1, localX, step);
			if (onNorth) pushOne(chunkX, chunkZ + 1, localX, 0);

			if (onWest && onSouth) pushOne(chunkX - 1, chunkZ - 1, step, step);
			if (onEast && onSouth) pushOne(chunkX + 1, chunkZ - 1, 0,    step);
			if (onWest && onNorth) pushOne(chunkX - 1, chunkZ + 1, step, 0);
			if (onEast && onNorth) pushOne(chunkX + 1, chunkZ + 1, 0,    0);
		}
	}

	void DistributeBilinearDelta(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		float posCellX, float posCellZ,
		float value,
		engine::editor::world::SparseChunkDeltas& outDeltas)
	{
		if (value == 0.0f) return;
		if (posCellX < 0.0f || posCellZ < 0.0f) return;
		if (posCellX > static_cast<float>(grid.width - 1) ||
			posCellZ > static_cast<float>(grid.height - 1)) return;

		const int x0 = static_cast<int>(std::floor(posCellX));
		const int z0 = static_cast<int>(std::floor(posCellZ));
		const int x1 = std::min(x0 + 1, grid.width - 1);
		const int z1 = std::min(z0 + 1, grid.height - 1);
		const float tx = posCellX - static_cast<float>(x0);
		const float tz = posCellZ - static_cast<float>(z0);

		// Poids bilinéaires : (1-tx)(1-tz), tx(1-tz), (1-tx)tz, tx tz.
		const float w00 = (1.0f - tx) * (1.0f - tz);
		const float w10 = tx * (1.0f - tz);
		const float w01 = (1.0f - tx) * tz;
		const float w11 = tx * tz;

		const int gx0 = grid.originCellX + x0;
		const int gx1 = grid.originCellX + x1;
		const int gz0 = grid.originCellZ + z0;
		const int gz1 = grid.originCellZ + z1;

		EmitAt(gx0, gz0, value * w00, outDeltas);
		EmitAt(gx1, gz0, value * w10, outDeltas);
		EmitAt(gx0, gz1, value * w01, outDeltas);
		EmitAt(gx1, gz1, value * w11, outDeltas);
	}
}
