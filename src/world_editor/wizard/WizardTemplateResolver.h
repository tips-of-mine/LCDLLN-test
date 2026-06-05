#pragma once

// M100.50 — WizardTemplateResolver : transforme les choix du wizard + seed en
// un ZonePreset (M100.46) prêt à passer au ZonePresetExecutor. Le template est
// défini EN CODE pour le MVP (pas de parseur JSON partagé) ; conditions "if",
// substitution {{var}} et auto-générateurs sont appliqués.

#include "src/world_editor/wizard/WizardChoices.h"
#include "src/world_editor/zone_presets/ZonePreset.h"

namespace engine::editor::world::wizard
{
	class WizardTemplateResolver
	{
	public:
		/// Résout les choix en un ZonePreset complet et valide. id déterministe
		/// dérivé des choix ; chaque combinaison (climat × relief × côte × POI)
		/// produit un preset distinct mais structurellement valide.
		engine::editor::world::zone_presets::ZonePreset Resolve(const WizardChoices& choices) const;
	};
}
