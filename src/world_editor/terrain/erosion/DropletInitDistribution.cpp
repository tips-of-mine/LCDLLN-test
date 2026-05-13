#include "src/world_editor/terrain/erosion/DropletInitDistribution.h"

#include "src/world_editor/water/D8FlowDirection.h"
#include "src/world_editor/water/FlowAccumulation.h"

#include <algorithm>
#include <cstddef>

namespace engine::editor::world::erosion
{
	void DropletInitDistribution::Init(
		const engine::editor::world::ConsolidatedHeightGrid& grid,
		DropletDistribution distribution, uint32_t rngSeed)
	{
		m_grid = &grid;
		m_distribution = distribution;
		m_rng.seed(rngSeed);
		m_cumulativeWeights.clear();

		if (distribution == DropletDistribution::Uniform) return;

		const size_t N = static_cast<size_t>(grid.width) * grid.height;
		m_cumulativeWeights.resize(N);

		if (distribution == DropletDistribution::WeightedAltitude)
		{
			// Poids = max(0, altitude - min(altitude)). Cellules à
			// l'altitude minimale ont poids 0 (rarement échantillonnées).
			float minH = grid.heights.empty() ? 0.0f : grid.heights[0];
			for (float h : grid.heights) minH = std::min(minH, h);
			float cumul = 0.0f;
			for (size_t i = 0; i < N; ++i)
			{
				const float w = std::max(0.0f, grid.heights[i] - minH);
				cumul += w;
				m_cumulativeWeights[i] = cumul;
			}
			// Si tout uniforme (cumul == 0), on tombe en uniforme implicite
			// via la branche `cdfTotal == 0` du Sample.
		}
		else if (distribution == DropletDistribution::WeightedFlowAccum)
		{
			// Réutilise le D8 + flow accumulation de M100.36.
			const auto flowDirs = engine::editor::world::ComputeD8FlowDirection(grid);
			const auto flowAcc  = engine::editor::world::ComputeFlowAccumulation(grid, flowDirs);
			float cumul = 0.0f;
			for (size_t i = 0; i < N; ++i)
			{
				const float w = static_cast<float>(flowAcc[i]);
				cumul += w;
				m_cumulativeWeights[i] = cumul;
			}
		}
	}

	std::pair<float, float> DropletInitDistribution::Sample()
	{
		if (m_grid == nullptr) return { 0.0f, 0.0f };
		const float W = static_cast<float>(m_grid->width - 1);
		const float H = static_cast<float>(m_grid->height - 1);

		if (m_distribution == DropletDistribution::Uniform || m_cumulativeWeights.empty())
		{
			std::uniform_real_distribution<float> distX(0.0f, W);
			std::uniform_real_distribution<float> distZ(0.0f, H);
			return { distX(m_rng), distZ(m_rng) };
		}

		const float cdfTotal = m_cumulativeWeights.back();
		if (cdfTotal <= 0.0f)
		{
			// Tous poids nuls → fallback uniforme.
			std::uniform_real_distribution<float> distX(0.0f, W);
			std::uniform_real_distribution<float> distZ(0.0f, H);
			return { distX(m_rng), distZ(m_rng) };
		}
		std::uniform_real_distribution<float> distU(0.0f, cdfTotal);
		const float u = distU(m_rng);
		auto it = std::lower_bound(m_cumulativeWeights.begin(),
			m_cumulativeWeights.end(), u);
		const size_t idx = static_cast<size_t>(it - m_cumulativeWeights.begin());
		const int x = static_cast<int>(idx % static_cast<size_t>(m_grid->width));
		const int z = static_cast<int>(idx / static_cast<size_t>(m_grid->width));
		// Léger jitter intra-cellule pour éviter le bias entier.
		std::uniform_real_distribution<float> distJ(0.0f, 1.0f);
		const float jx = distJ(m_rng);
		const float jz = distJ(m_rng);
		return {
			std::min(static_cast<float>(x) + jx, W),
			std::min(static_cast<float>(z) + jz, H)
		};
	}
}
