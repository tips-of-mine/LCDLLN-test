#pragma once

// M100.48 — Interface d'une règle de validation. Chaque catégorie de problème
// est une règle indépendante, enregistrée dans le ValidationRuleRegistry.
// Architecture extensible : ajouter une règle = créer une classe + l'enregistrer,
// sans modifier le service.

#include <string>
#include <vector>

#include "src/world_editor/validation/ValidationTypes.h"

namespace engine::editor::world::validation
{
	/// Règle de validation : inspecte le contexte (lecture seule) et ajoute des
	/// problèmes à la liste de sortie.
	class IValidationRule
	{
	public:
		virtual ~IValidationRule() = default;

		/// Identifiant stable de la règle (ex. "heightmap.holes").
		virtual const char* GetRuleId() const = 0;
		/// Catégorie (ex. "heightmap", "splat", "mesh_inserts").
		virtual const char* GetCategory() const = 0;
		/// Description courte de ce que la règle détecte.
		virtual const char* GetDescription() const = 0;
		/// Sévérité par défaut des problèmes émis par cette règle.
		virtual Severity GetDefaultSeverity() const = 0;

		/// Tags des données inspectées (ex. {"terrain"}). Permet l'invalidation
		/// incrémentale : si un tag change, la règle est re-runée.
		virtual std::vector<std::string> GetDocumentTags() const = 0;

		/// Exécute la validation ; ajoute les problèmes trouvés à `issuesOut`.
		virtual void Run(const ValidationContext& ctx, std::vector<ValidationIssue>& issuesOut) const = 0;
	};
}
