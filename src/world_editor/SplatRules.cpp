#include "src/world_editor/SplatRules.h"

#include <algorithm>
#include <cmath>

namespace engine::editor::world
{
	float ComputeSlopeDeg(const engine::world::terrain::TerrainChunk& chunk,
		uint32_t x, uint32_t z)
	{
		const uint32_t rx = chunk.resolutionX;
		const uint32_t rz = chunk.resolutionZ;
		if (rx == 0 || rz == 0 || x >= rx || z >= rz) return 0.0f;

		// Voisins immédiats (clamp aux bords par duplication, identique à
		// `TerrainMeshBuilder::ComputeNormalAndUv`).
		const uint32_t xL = (x == 0u) ? 0u : x - 1u;
		const uint32_t xR = std::min(x + 1u, rx - 1u);
		const uint32_t zL = (z == 0u) ? 0u : z - 1u;
		const uint32_t zR = std::min(z + 1u, rz - 1u);

		const float hL = chunk.heights[static_cast<size_t>(z) * rx + xL];
		const float hR = chunk.heights[static_cast<size_t>(z) * rx + xR];
		const float hD = chunk.heights[static_cast<size_t>(zL) * rx + x];
		const float hU = chunk.heights[static_cast<size_t>(zR) * rx + x];

		// Span effectif en mètres : 2*cellSize au centre, cellSize aux bords.
		const float spanX = static_cast<float>(xR - xL) * chunk.cellSizeMeters;
		const float spanZ = static_cast<float>(zR - zL) * chunk.cellSizeMeters;
		const float invSpanX = (spanX > 0.0f) ? (1.0f / spanX) : 0.0f;
		const float invSpanZ = (spanZ > 0.0f) ? (1.0f / spanZ) : 0.0f;
		const float dhdx = (hR - hL) * invSpanX;
		const float dhdz = (hU - hD) * invSpanZ;

		// tan(slope) = sqrt(dhdx² + dhdz²) ; slope = atan(...) en radians,
		// puis conversion degrés. Borné [0, 90°] par la définition d'atan
		// sur un argument >=0.
		const float tanSlope = std::sqrt(dhdx * dhdx + dhdz * dhdz);
		const float radians = std::atan(tanSlope);
		constexpr float kRadToDeg = 57.2957795130823208768f;
		return radians * kRadToDeg;
	}

	bool MatchesRules(const engine::world::terrain::TerrainChunk& chunk,
		uint32_t x, uint32_t z,
		float slopeMinDeg, float slopeMaxDeg,
		float altMin, float altMax)
	{
		const uint32_t rx = chunk.resolutionX;
		const uint32_t rz = chunk.resolutionZ;
		if (x >= rx || z >= rz) return false;

		const float alt = chunk.heights[static_cast<size_t>(z) * rx + x];
		if (alt < altMin || alt > altMax) return false;

		const float slope = ComputeSlopeDeg(chunk, x, z);
		if (slope < slopeMinDeg || slope > slopeMaxDeg) return false;

		return true;
	}
}
