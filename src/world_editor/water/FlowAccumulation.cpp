#include "src/world_editor/water/FlowAccumulation.h"

#include "src/world_editor/water/D8FlowDirection.h"

#include <algorithm>
#include <cstddef>

namespace engine::editor::world
{
	std::vector<uint32_t> ComputeFlowAccumulation(
		const ConsolidatedHeightGrid& grid,
		const std::vector<uint8_t>& flowDirs)
	{
		const int W = grid.width;
		const int H = grid.height;
		const size_t N = static_cast<size_t>(W) * static_cast<size_t>(H);

		std::vector<uint32_t> flowAcc(N, 1u);
		if (flowDirs.size() != N || W < 1 || H < 1) return flowAcc;

		// Tri stable des indices cellules par altitude décroissante. On
		// utilise std::sort + comparateur stable (lexico fallback sur l'index
		// pour garantir un ordre reproductible byte-à-byte). Le compteur
		// uint32 sur l'index permet d'aller jusqu'à 2^32 cellules (largement
		// au-dessus d'une zone 5121² = ~26 M).
		std::vector<uint32_t> sorted(N);
		for (uint32_t i = 0; i < static_cast<uint32_t>(N); ++i) sorted[i] = i;
		std::sort(sorted.begin(), sorted.end(),
			[&](uint32_t a, uint32_t b) {
				const float ha = grid.heights[a];
				const float hb = grid.heights[b];
				if (ha != hb) return ha > hb;     // altitude décroissante
				return a < b;                     // tie-break déterministe
			});

		// Propagation : pour chaque cellule (de la plus haute à la plus basse),
		// transfère son flow accumulé à la cellule aval (selon D8). Les
		// cellules de plus haute altitude n'ont aucun amont, donc démarrent à 1.
		for (uint32_t idx : sorted)
		{
			const uint8_t dir = flowDirs[idx];
			if (dir >= 8u) continue;             // sink ou hors plage
			const int x = static_cast<int>(idx % static_cast<uint32_t>(W));
			const int z = static_cast<int>(idx / static_cast<uint32_t>(W));
			const int nx = x + kD8Order[dir].dx;
			const int nz = z + kD8Order[dir].dz;
			if (nx < 0 || nx >= W || nz < 0 || nz >= H) continue;
			const size_t downIdx = static_cast<size_t>(nz) * W + nx;
			flowAcc[downIdx] += flowAcc[idx];
		}

		return flowAcc;
	}
}
