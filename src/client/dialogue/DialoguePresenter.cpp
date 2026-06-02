#include "src/client/dialogue/DialoguePresenter.h"

#include <algorithm>

namespace engine::client
{
	void DialoguePresenter::OpenDialogue(const DialogueTree& tree, const DialogueNpcRef& npc)
	{
		m_tree            = tree;
		m_npc             = npc;
		m_active          = true;
		m_lastCloseReason = DialogueCloseReason::None;
		m_scrollOffset    = 0.0f;
		m_contentHeight   = 0.0f;
		m_viewHeight      = 0.0f;
		m_autoScroll      = true;
		SetCurrentNode(m_tree.startNodeId);
	}

	void DialoguePresenter::SetCurrentNode(const std::string& id)
	{
		m_current      = m_tree.FindNode(id);
		m_scrollOffset = 0.0f; // nouveau texte : on repart du haut
		m_autoScroll   = true;
	}

	void DialoguePresenter::SelectChoice(size_t index)
	{
		if (!m_active || m_current == nullptr || index >= m_current->choices.size())
			return;

		const DialogueChoice& choice = m_current->choices[index];

		// Journalisation si lié à une quête/raid. On consigne AVANT de déclencher
		// l'action quête : le callback est best-effort (retour void, pas d'échec
		// remonté), donc l'ordre n'altère pas la cohérence — on garde une trace de
		// la conversation même si le système quête traite l'action de façon asynchrone.
		const bool questLinked = (choice.action == DialogueAction::AcceptQuest)
		                       || (choice.action == DialogueAction::CompleteQuest)
		                       || (choice.questId >= 0);
		if (questLinked && m_journalSink != nullptr)
		{
			QuestConversationEntry e;
			e.npcLabel   = m_npc.label;
			e.questId    = choice.questId;
			e.choiceText = choice.text;
			for (const DialogueLine& l : m_current->lines)
				e.lines.push_back(l.text);
			m_journalSink->RecordConversation(e);
		}

		// Déclenche l'action quête côté système (accept/complete).
		if ((choice.action == DialogueAction::AcceptQuest
		     || choice.action == DialogueAction::CompleteQuest)
		    && m_questActionCb)
		{
			m_questActionCb(choice.action, choice.questId);
		}

		// Navigation / fermeture.
		if (choice.action == DialogueAction::End)
		{
			Close(DialogueCloseReason::EndNode);
			return;
		}

		if (!choice.nextNodeId.empty() && m_tree.FindNode(choice.nextNodeId) != nullptr)
		{
			SetCurrentNode(choice.nextNodeId);
		}
		else
		{
			// Pas de suite valide (ex. accept_quest sans next) => fin naturelle.
			Close(DialogueCloseReason::EndNode);
		}
	}

	void DialoguePresenter::Close(DialogueCloseReason reason)
	{
		m_active          = false;
		m_lastCloseReason = reason;
		m_current         = nullptr;
	}

	void DialoguePresenter::Tick(float deltaSeconds, float distanceMeters)
	{
		if (!m_active)
			return;

		// Rupture de distance (filet de sécurité), avec hystérésis anti-clignotement.
		if (distanceMeters > (kMaxDistanceMeters + kHysteresisMeters))
		{
			Close(DialogueCloseReason::TooFar);
			return;
		}

		// Auto-scroll fluide vers le bas.
		if (m_autoScroll)
		{
			const float maxOffset = std::max(0.0f, m_contentHeight - m_viewHeight);
			m_scrollOffset += kAutoScrollPixelsPerSecond * deltaSeconds;
			if (m_scrollOffset >= maxOffset)
				m_scrollOffset = maxOffset;
		}
	}

	void DialoguePresenter::SetViewMetrics(float contentHeight, float viewHeight)
	{
		m_contentHeight = contentHeight;
		m_viewHeight    = viewHeight;
		const float maxOffset = std::max(0.0f, m_contentHeight - m_viewHeight);
		if (m_scrollOffset > maxOffset)
			m_scrollOffset = maxOffset;
	}

	void DialoguePresenter::OnUserScroll(float newOffset)
	{
		const float maxOffset = std::max(0.0f, m_contentHeight - m_viewHeight);
		m_scrollOffset = std::max(0.0f, std::min(newOffset, maxOffset));
		// Pause si l'utilisateur n'est pas tout en bas ; reprise s'il y revient.
		m_autoScroll = (m_scrollOffset >= maxOffset - 0.5f);
	}

} // namespace engine::client
