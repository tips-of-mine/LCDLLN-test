#pragma once

// M100.49 — TutorialIo : source des définitions de tutoriel. Le MVP fournit le
// tutoriel "first_launch" défini EN CODE (BuildFirstLaunchTutorial, déclaré dans
// Tutorial.h). Le chargement depuis `game/data/editor/tutorials/*.json` est
// différé (2e passe) ; le fichier JSON committé documente le contenu attendu.
//
// Ce header expose un helper de chargement par id qui, pour le MVP, route vers
// le builder code-défini.

#include <optional>
#include <string>

#include "src/world_editor/tutorial/Tutorial.h"

namespace engine::editor::world::tutorial
{
	/// Charge un tutoriel par id. MVP : seul "first_launch" est connu (code).
	/// Retourne nullopt si l'id est inconnu.
	std::optional<Tutorial> LoadTutorialById(const std::string& id);
}
