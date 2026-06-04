#pragma once

// M100.48 — Registre des règles de validation. Instanciable (pas un singleton
// global) pour rester test-friendly : chaque test construit son propre registre.
// L'éditeur en tient une instance et appelle RegisterMvpValidationRules au boot.

#include <memory>
#include <string>
#include <vector>

#include "src/world_editor/validation/IValidationRule.h"

namespace engine::editor::world::validation
{
	/// Conteneur ordonné de règles (ownership). L'ordre d'enregistrement est
	/// préservé ; le tri final par sévérité est fait par ZoneValidator.
	class ValidationRuleRegistry
	{
	public:
		/// Ajoute une règle (prend l'ownership). Ignore un pointeur nul.
		void RegisterRule(std::unique_ptr<IValidationRule> rule);

		/// Toutes les règles enregistrées (pointeurs non-owning, ordre d'ajout).
		const std::vector<IValidationRule*>& GetAllRules() const { return m_rulesView; }

		/// Règles d'une catégorie donnée.
		std::vector<IValidationRule*> GetRulesByCategory(const std::string& category) const;

		/// Nombre de règles enregistrées.
		size_t Count() const { return m_rules.size(); }

	private:
		std::vector<std::unique_ptr<IValidationRule>> m_rules;
		std::vector<IValidationRule*>                 m_rulesView;
	};

	/// Enregistre toutes les règles MVP (heightmap, splat, mesh inserts) dans
	/// `registry`. Appelée au boot de l'éditeur et par les tests.
	void RegisterMvpValidationRules(ValidationRuleRegistry& registry);
}
