#pragma once

// M100.50 — QuickStartWizard : machine d'état des 5 étapes guidées (logique
// PURE, testable headless). L'UI (cartes de choix, aperçu 3D) est une passe
// séparée ; la résolution choix → ZonePreset est dans WizardTemplateResolver.

#include <cstdint>
#include <string>

#include "src/world_editor/wizard/WizardChoices.h"

namespace engine::editor::world::wizard
{
	/// Les 5 étapes du wizard.
	enum class WizardStep : uint8_t
	{
		Climate = 0,
		Relief  = 1,
		Coast   = 2,
		Poi     = 3,
		Preview = 4,
	};

	/// Machine d'état du wizard. Valide chaque choix, autorise next/prev, et
	/// signale quand on peut générer.
	class QuickStartWizard
	{
	public:
		WizardStep CurrentStep() const { return m_step; }
		const WizardChoices& Choices() const { return m_choices; }
		WizardChoices& Choices() { return m_choices; }

		/// Affecte le choix de l'étape courante (ignore les étapes Preview).
		/// \return true si la valeur est valide pour l'étape courante.
		bool SetChoiceForCurrentStep(const std::string& value);

		/// True si l'étape courante a un choix valide → on peut avancer.
		/// L'étape Preview peut toujours avancer (= générer).
		bool CanProceed() const;

		/// Avance d'une étape si possible. \return true si on a avancé.
		bool Next();

		/// Recule d'une étape si possible. \return true si on a reculé.
		bool Prev();

		/// True si on est à l'étape Preview avec tous les choix valides : prêt à
		/// générer la zone.
		bool IsReadyToGenerate() const;

		/// Fixe le seed (étape Preview).
		void SetSeed(uint32_t seed) { m_choices.seed = seed; }

	private:
		WizardStep    m_step = WizardStep::Climate;
		WizardChoices m_choices;
	};
}
