// M100.50 — Implémentation WizardChoices (validation) + QuickStartWizard (FSM).

#include "src/world_editor/wizard/QuickStartWizard.h"

namespace engine::editor::world::wizard
{
	bool IsValidClimate(const std::string& v)
	{
		return v == "temperate" || v == "arid" || v == "polar" || v == "tropical";
	}
	bool IsValidRelief(const std::string& v)
	{
		return v == "plains" || v == "hills" || v == "mountains" || v == "escarped";
	}
	bool IsValidCoast(const std::string& v)
	{
		return v == "interior" || v == "moderate" || v == "dramatic";
	}
	bool IsValidPoi(const std::string& v)
	{
		return v == "none" || v == "cave" || v == "ruin" || v == "dungeon";
	}

	bool QuickStartWizard::SetChoiceForCurrentStep(const std::string& value)
	{
		switch (m_step)
		{
		case WizardStep::Climate:
			if (!IsValidClimate(value)) return false;
			m_choices.climate = value; return true;
		case WizardStep::Relief:
			if (!IsValidRelief(value)) return false;
			m_choices.relief = value; return true;
		case WizardStep::Coast:
			if (!IsValidCoast(value)) return false;
			m_choices.coast = value; return true;
		case WizardStep::Poi:
			if (!IsValidPoi(value)) return false;
			m_choices.poi = value; return true;
		case WizardStep::Preview:
		default:
			return false; // Preview ne porte pas de choix textuel.
		}
	}

	bool QuickStartWizard::CanProceed() const
	{
		switch (m_step)
		{
		case WizardStep::Climate: return IsValidClimate(m_choices.climate);
		case WizardStep::Relief:  return IsValidRelief(m_choices.relief);
		case WizardStep::Coast:   return IsValidCoast(m_choices.coast);
		case WizardStep::Poi:     return IsValidPoi(m_choices.poi);
		case WizardStep::Preview: return true;
		default:                  return false;
		}
	}

	bool QuickStartWizard::Next()
	{
		if (m_step == WizardStep::Preview) return false; // dernière étape.
		if (!CanProceed()) return false;
		m_step = static_cast<WizardStep>(static_cast<uint8_t>(m_step) + 1);
		return true;
	}

	bool QuickStartWizard::Prev()
	{
		if (m_step == WizardStep::Climate) return false;
		m_step = static_cast<WizardStep>(static_cast<uint8_t>(m_step) - 1);
		return true;
	}

	bool QuickStartWizard::IsReadyToGenerate() const
	{
		return m_step == WizardStep::Preview
			&& IsValidClimate(m_choices.climate)
			&& IsValidRelief(m_choices.relief)
			&& IsValidCoast(m_choices.coast)
			&& IsValidPoi(m_choices.poi);
	}
}
