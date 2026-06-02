#pragma once

#include "src/client/dialogue/DialogueTree.h"

#include <functional>
#include <string>
#include <vector>

namespace engine::client
{
	/// Référence légère vers le PNJ avec qui on dialogue.
	struct DialogueNpcRef
	{
		std::string label; ///< Nom affiché.
		std::string role;  ///< Sous-titre (ex. "Garde du pont").
		int         entityIndex = -1; ///< Index dans m_interactables (pour distance).
	};

	/// Raison de fermeture du dialogue.
	enum class DialogueCloseReason
	{
		None,
		UserClose, ///< ✕ ou Échap.
		TooFar,    ///< Joueur > seuil de distance.
		EndNode    ///< Choix End ou nœud terminal.
	};

	/// Entrée de conversation à consigner dans le journal de quête.
	struct QuestConversationEntry
	{
		std::string              npcLabel;
		int                      questId = -1;
		std::vector<std::string> lines;      ///< Texte échangé (lignes du nœud déclencheur).
		std::string              choiceText; ///< Choix retenu.
	};

	/// Puits de journalisation (découplé du système de fichiers pour testabilité).
	class IQuestConversationSink
	{
	public:
		virtual ~IQuestConversationSink() = default;
		virtual void RecordConversation(const QuestConversationEntry& entry) = 0;
	};

	/// Presenter runtime du dialogue PNJ. Logique pure (aucune dépendance ImGui).
	class DialoguePresenter final
	{
	public:
		/// Distance nominale max joueur↔PNJ (mètres). Au-delà, rupture.
		static constexpr float kMaxDistanceMeters = 1.5f;
		/// Hystérésis : rupture effective à kMaxDistanceMeters + kHysteresisMeters.
		static constexpr float kHysteresisMeters = 0.1f;
		/// Vitesse d'auto-scroll (pixels/seconde) — « doucement ».
		static constexpr float kAutoScrollPixelsPerSecond = 20.0f;

		/// Ouvre un dialogue (positionne le nœud de départ, active le presenter).
		void OpenDialogue(const DialogueTree& tree, const DialogueNpcRef& npc);

		/// Sélectionne le choix d'indice donné dans le nœud courant.
		/// Applique l'action (journalise si lié à une quête, fire le callback quête),
		/// puis navigue ou ferme.
		void SelectChoice(size_t index);

		/// Avance l'auto-scroll et applique la rupture de distance.
		/// \param deltaSeconds temps écoulé.
		/// \param distanceMeters distance XZ courante joueur↔PNJ.
		void Tick(float deltaSeconds, float distanceMeters);

		/// Ferme le dialogue avec une raison.
		void Close(DialogueCloseReason reason);

		// --- Lecture (pour le renderer) ---
		bool                IsActive() const { return m_active; }
		DialogueCloseReason LastCloseReason() const { return m_lastCloseReason; }
		const DialogueNpcRef& Npc() const { return m_npc; }
		const DialogueNode*   CurrentNode() const { return m_current; }
		float                 ScrollOffset() const { return m_scrollOffset; }
		bool                  AutoScrollEnabled() const { return m_autoScroll; }

		// --- Auto-scroll piloté par le renderer ---
		/// Renseigne les métriques de la zone texte (hauteur contenu/vue, en pixels).
		void SetViewMetrics(float contentHeight, float viewHeight);
		/// L'utilisateur a fait défiler manuellement : met l'auto-scroll en pause.
		void OnUserScroll(float newOffset);

		// --- Callbacks ---
		/// Appelé quand un choix porte AcceptQuest/CompleteQuest (déclenche le système quête).
		void SetQuestActionCallback(std::function<void(DialogueAction, int /*questId*/)> cb)
		{
			m_questActionCb = std::move(cb);
		}
		/// Puits de journalisation (non possédé). Peut être nullptr (pas de journal).
		void SetJournalSink(IQuestConversationSink* sink) { m_journalSink = sink; }

	private:
		void SetCurrentNode(const std::string& id);

		DialogueTree         m_tree;
		const DialogueNode*  m_current = nullptr;
		DialogueNpcRef       m_npc;
		bool                 m_active = false;
		DialogueCloseReason  m_lastCloseReason = DialogueCloseReason::None;

		// Auto-scroll
		float m_scrollOffset  = 0.0f;
		float m_contentHeight = 0.0f;
		float m_viewHeight    = 0.0f;
		bool  m_autoScroll    = true;

		std::function<void(DialogueAction, int)> m_questActionCb;
		IQuestConversationSink*                  m_journalSink = nullptr;
	};

} // namespace engine::client
