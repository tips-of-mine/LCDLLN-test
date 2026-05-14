#include "src/world_editor/water/CoastlineCliffs.h"

#include "src/client/world/terrain/TerrainChunk.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	namespace
	{
		constexpr int kRes =
			static_cast<int>(engine::world::terrain::kTerrainResolution);

		float SampleClamped(const ConsolidatedHeightGrid& g, int x, int z)
		{
			const int cx = std::clamp(x, 0, g.width - 1);
			const int cz = std::clamp(z, 0, g.height - 1);
			return g.Get(cx, cz);
		}

		/// Pente locale en degrés (différences finies centrées 1-cell).
		float SlopeDegrees(const ConsolidatedHeightGrid& g, int x, int z)
		{
			const float hL = SampleClamped(g, x - 1, z);
			const float hR = SampleClamped(g, x + 1, z);
			const float hD = SampleClamped(g, x, z - 1);
			const float hU = SampleClamped(g, x, z + 1);
			const float dx = (hR - hL) / (2.0f * g.cellSizeMeters);
			const float dz = (hU - hD) / (2.0f * g.cellSizeMeters);
			const float magnitude = std::sqrt(dx * dx + dz * dz);
			return std::atan(magnitude) * (180.0f / 3.14159265359f);
		}

		void EmitDelta(SparseChunkDeltas& out, int cellX, int cellZ, float delta)
		{
			if (delta == 0.0f) return;
			const int chunkX = cellX / (kRes - 1);
			const int chunkZ = cellZ / (kRes - 1);
			const int localX = cellX - chunkX * (kRes - 1);
			const int localZ = cellZ - chunkZ * (kRes - 1);
			const engine::world::GlobalChunkCoord coord{ chunkX, chunkZ };
			const uint32_t idx = static_cast<uint32_t>(localZ * kRes + localX);
			out[coord][idx] += delta;
		}
	}

	SparseChunkDeltas ComputeCoastlineCliffsDeltas(
		const ConsolidatedHeightGrid& pristineGrid,
		float seaLevelMeters,
		float thresholdMeters,
		float slopeThresholdDeg,
		float cliffLandSideMeters,
		float cliffSeaSideMeters)
	{
		SparseChunkDeltas out;
		if (pristineGrid.width < 2 || pristineGrid.height < 2) return out;
		if (thresholdMeters <= 0.0f) return out;

		const float seaLow  = seaLevelMeters - thresholdMeters;
		const float seaHigh = seaLevelMeters + thresholdMeters;

		for (int z = 0; z < pristineGrid.height; ++z)
		{
			for (int x = 0; x < pristineGrid.width; ++x)
			{
				const float h = pristineGrid.Get(x, z);
				if (h < seaLow || h > seaHigh) continue;
				const float slope = SlopeDegrees(pristineGrid, x, z);
				if (slope < slopeThresholdDeg) continue;

				// `u` = distance normalisée |h - sea| / threshold ∈ [0, 1].
				// Plus on est près du sea level, plus l'effet est fort.
				const float uRaw = std::fabs(h - seaLevelMeters) / thresholdMeters;
				const float u    = std::clamp(uRaw, 0.0f, 1.0f);
				const float weight = 1.0f - u * u * (3.0f - 2.0f * u); // 1 - smoothstep(0,1,u)
				if (weight <= 0.0f) continue;

				const float delta = (h > seaLevelMeters)
					? (+cliffLandSideMeters * weight)
					: (-cliffSeaSideMeters  * weight);

				const int cellX = pristineGrid.originCellX + x;
				const int cellZ = pristineGrid.originCellZ + z;
				EmitDelta(out, cellX, cellZ, delta);
			}
		}
		return out;
	}
}
