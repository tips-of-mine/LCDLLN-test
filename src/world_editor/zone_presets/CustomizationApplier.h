#pragma once

#include "src/world_editor/zone_presets/OperationParams.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::editor::world::zone_presets
{
	/// Curseurs globaux de customisation d'un zone preset (M100.46 §D.1).
	/// Tweakés par le mappeur dans le dialog avant exécution.
	struct CustomizationParams
	{
		float    reliefMultiplier       = 1.0f; ///< ×hauteurs / amplitudes / profondeurs
		float    waterDensityMultiplier = 1.0f; ///< ×gouttes hydraulic, seuils de flow
		float    drynessMultiplier      = 1.0f; ///< ×évaporation, ×force du vent
		uint32_t seed                   = 42u;  ///< RNG seed déterministe global

		/// true si aucun curseur ne dévie de 1.0 (exécution « preset brut »).
		bool IsNeutral() const;
	};

	/// Applique les multiplicateurs de customisation aux paramètres
	/// **scalaires** d'une opération, in-place, selon ses tags
	/// `affectedBy` (M100.46 §D.2). Une opération sans tag pertinent n'est
	/// pas touchée.
	///
	/// Champs scalaires concernés (mis à l'échelle si la clé existe et est
	/// un nombre) :
	///   - tag `"relief"`        → `heightMeters`, `depthMeters`, `amplitudeMeters`
	///   - tag `"water_density"` → `numDroplets`, `flowAccumThreshold`
	///   - tag `"dryness"`       → `evaporationRate` (×), `windStrength` (×)
	///
	/// Note : les effets sur des *comptes de listes* (nombre de sources de
	/// rivière, nombre de rivières) mentionnés dans le spec §D.2 ne sont
	/// pas appliqués ici — ils relèvent du `OperationDispatcher` au moment
	/// de la construction de la commande (incrément suivant), car ils
	/// modifient la taille de listes, pas une valeur scalaire.
	void ApplyCustomization(OperationParams& params,
		const std::vector<std::string>& affectedBy,
		const CustomizationParams& custom);

	/// true si `affectedBy` contient `tag`.
	bool HasAffectedByTag(const std::vector<std::string>& affectedBy,
		const std::string& tag);
}
