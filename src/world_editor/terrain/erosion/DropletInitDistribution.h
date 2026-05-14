#pragma once

#include "src/world_editor/terrain/erosion/HydraulicSimulationParams.h"
#include "src/world_editor/water/ConsolidatedHeightGrid.h"

#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace engine::editor::world::erosion
{
	/// Tireur de positions initiales pour les gouttes (M100.38). Selon le
	/// mode, le tirage est uniforme ou pondéré (par altitude ou par flow
	/// accumulation). Le tirage utilise un `std::mt19937` seedé déterministe.
	class DropletInitDistribution
	{
	public:
		/// Initialise le tireur sur le grid donné en pré-calculant un CDF
		/// si nécessaire (uniquement pour `WeightedAltitude` et
		/// `WeightedFlowAccum`). Coût O(N) pour le pré-calcul.
		///
		/// \param grid           Grid consolidé (lecture seule).
		/// \param distribution   Mode de distribution.
		/// \param rngSeed        Seed pour reproductibilité.
		void Init(const engine::editor::world::ConsolidatedHeightGrid& grid,
			DropletDistribution distribution, uint32_t rngSeed);

		/// Tire une position cellule (fractionnaire) selon le mode courant.
		std::pair<float, float> Sample();

	private:
		const engine::editor::world::ConsolidatedHeightGrid* m_grid = nullptr;
		DropletDistribution m_distribution = DropletDistribution::Uniform;
		std::mt19937        m_rng{ 42u };
		/// CDF cumulé sur les poids des cellules (taille `width × height`).
		/// Vide en mode `Uniform`. Sample = lower_bound sur ce CDF.
		std::vector<float>  m_cumulativeWeights;
	};
}
