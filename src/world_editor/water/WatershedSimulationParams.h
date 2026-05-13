#pragma once

#include "src/world_editor/water/SpringSource.h"

#include <cstdint>
#include <vector>

namespace engine::editor::world
{
	/// Paramètres complets d'une simulation watershed M100.36.
	///
	/// **Important** (spec §"Contexte critique §5") : `seaLevelMeters` n'est
	/// PAS un champ de cette struct. Le simulateur le lit depuis
	/// `WaterDocument::GetOcean().seaLevelMeters` au moment du `Run`. Cela
	/// garantit une source de vérité unique pour la zone (édité aussi par
	/// l'outil Coastline M100.37).
	struct WatershedSimulationParams
	{
		std::vector<SpringSource> springs;

		/// Une cellule dont la flow accumulation max le long du chemin est
		/// strictement < ce seuil voit le chemin rejeté (rivière trop maigre).
		uint32_t minFlowThresholdCells          = 200u;

		/// Tolérance de simplification Douglas-Peucker en mètres. Plus haute
		/// = polyline plus grossière.
		float    simplificationToleranceMeters  = 5.0f;

		/// Si vrai, un chemin qui termine sur un sink (minimum local) génère
		/// une `LakeInstance` couvrant le bassin local.
		bool     autoLakesAtSinks               = true;
		float    autoLakeMaxDepthMeters         = 10.0f;

		/// Si vrai, le terrain est creusé le long du tracé pour donner un lit
		/// de rivière visible (profil gaussien transverse).
		bool     carveHeightmap                 = true;
		float    carveDepthMeters               = 3.0f;
		float    carveWidthMeters               = 12.0f;
	};
}
