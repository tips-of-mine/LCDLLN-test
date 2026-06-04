// M100.49 — Implémentation TutorialSystem.

#include "src/world_editor/tutorial/TutorialSystem.h"

namespace engine::editor::world::tutorial
{
	using engine::editor::world::help::OverlayGuidanceSystem;
	using engine::editor::world::help::OverlayInstruction;

	void TutorialSystem::StartFrom(OverlayGuidanceSystem& overlay, size_t fromStep)
	{
		std::vector<OverlayInstruction> instructions;
		instructions.reserve(m_tutorial.steps.size());
		for (size_t i = fromStep; i < m_tutorial.steps.size(); ++i)
			instructions.push_back(m_tutorial.steps[i].instruction);

		m_state = TutorialState::Running;
		overlay.StartSequence(std::move(instructions), [this]() { OnSequenceComplete(); });
	}

	void TutorialSystem::Start(OverlayGuidanceSystem& overlay)
	{
		m_savedProgress = 0;
		StartFrom(overlay, 0);
	}

	void TutorialSystem::Resume(OverlayGuidanceSystem& overlay)
	{
		const size_t from = m_savedProgress < m_tutorial.steps.size() ? m_savedProgress : 0;
		StartFrom(overlay, from);
	}

	void TutorialSystem::PauseFrom(OverlayGuidanceSystem& overlay)
	{
		// L'index overlay est relatif à la sous-séquence démarrée ; on l'ajoute à
		// la progression de départ pour retrouver l'index absolu d'étape.
		m_savedProgress += overlay.CurrentIndex();
		if (m_savedProgress > m_tutorial.steps.size())
			m_savedProgress = m_tutorial.steps.size();
		overlay.AbortSequence();
		m_state = TutorialState::Paused;
	}

	void TutorialSystem::Skip()
	{
		m_state = TutorialState::Skipped;
		SetFlag(kFirstLaunchFlag, "skipped");
	}

	void TutorialSystem::OnSequenceComplete()
	{
		m_state = TutorialState::Completed;
		m_savedProgress = m_tutorial.steps.size();
		SetFlag(kFirstLaunchFlag, "completed");
	}
}
