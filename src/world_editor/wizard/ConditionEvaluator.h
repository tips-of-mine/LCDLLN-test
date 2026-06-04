#pragma once

// M100.50 — Évaluation des conditions "if" et substitution des variables
// {{var}} du template du wizard. Pur, testable.

#include <string>

#include "src/world_editor/wizard/WizardChoices.h"

namespace engine::editor::world::wizard
{
	/// Remplace les placeholders {{climate}}, {{relief}}, {{coast}}, {{poi}},
	/// {{seed}} dans `text` par les valeurs de `choices`. Les placeholders
	/// inconnus sont laissés tels quels.
	std::string SubstituteVariables(const std::string& text, const WizardChoices& choices);

	/// Évalue une condition simple de la forme `field op 'value'` où `field` ∈
	/// {climate, relief, coast, poi}, `op` ∈ {==, !=}. Espaces tolérés. Une
	/// condition vide ou non reconnue est considérée vraie (opération non gardée).
	bool EvaluateCondition(const std::string& condition, const WizardChoices& choices);
}
