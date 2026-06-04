// M100.49 — Implémentation OverlayGuidanceSystem (moteur de séquence pur).

#include "src/world_editor/help/OverlayGuidanceSystem.h"

#include <utility>

namespace engine::editor::world::help
{
	void OverlayGuidanceSystem::StartSequence(std::vector<OverlayInstruction> instructions, OnCompleteCallback onComplete)
	{
		m_instructions = std::move(instructions);
		m_onComplete = std::move(onComplete);
		m_index = 0;
		m_active = !m_instructions.empty();
		if (!m_active && m_onComplete)
		{
			// Séquence vide : considérée immédiatement complète.
			auto cb = m_onComplete;
			m_onComplete = nullptr;
			cb();
		}
	}

	void OverlayGuidanceSystem::AdvanceStep()
	{
		if (!m_active) return;
		if (m_index + 1 < m_instructions.size())
		{
			++m_index;
			return;
		}
		// Dernière étape franchie → complétion.
		m_active = false;
		auto cb = m_onComplete;
		m_onComplete = nullptr;
		m_index = 0;
		m_instructions.clear();
		if (cb) cb();
	}

	bool OverlayGuidanceSystem::NotifyAction(const WidgetTargetId& widgetId)
	{
		if (!m_active) return false;
		const OverlayInstruction& cur = m_instructions[m_index];
		// Avance si l'étape attend explicitement cette action, ou si son widget
		// cible correspond (cas où validatesOnAction n'est pas renseigné).
		const bool matches =
			(!cur.validatesOnAction.empty() && cur.validatesOnAction == widgetId) ||
			(cur.validatesOnAction.empty() && !cur.targetWidget.empty() && cur.targetWidget == widgetId);
		if (!matches) return false;
		AdvanceStep();
		return true;
	}

	void OverlayGuidanceSystem::AbortSequence()
	{
		m_active = false;
		m_index = 0;
		m_instructions.clear();
		m_onComplete = nullptr;
	}
}
