#pragma once

#include "src/client/dialogue/DialoguePresenter.h" // IQuestConversationSink, QuestConversationEntry

#include <cstdint>
#include <string>

namespace engine::core { class Config; }

namespace engine::client
{
	/// Journal local des conversations de quête, une ligne JSON par entrée
	/// (fichier `<paths.content>/quest_journal/character_<id>.jsonl`).
	/// Implémente \ref IQuestConversationSink : branché sur le presenter, il
	/// persiste chaque conversation liée à une quête/raid pour le suivi.
	class QuestConversationJournal final : public IQuestConversationSink
	{
	public:
		/// \param config pour résoudre `paths.content` et l'écriture relative.
		/// \param characterId identifiant du personnage courant.
		/// \warning \p config est conservée par référence ; elle doit rester valide
		///          pour toute la durée de vie de ce journal (même contrat que
		///          \ref HudLayoutEditor).
		QuestConversationJournal(const engine::core::Config& config, uint64_t characterId);

		/// Sérialise l'entrée en une ligne JSON et l'ajoute au fichier du personnage.
		/// Effet de bord : écriture disque (append). Logue en cas d'échec.
		void RecordConversation(const QuestConversationEntry& entry) override;

		/// Sérialise une entrée en une ligne JSON compacte (pur, testable, sans I/O).
		static std::string SerializeEntryLine(const QuestConversationEntry& entry);

		/// Chemin relatif (sous paths.content) du fichier journal d'un personnage.
		static std::string RelPathForCharacter(uint64_t characterId);

	private:
		const engine::core::Config& m_config;
		uint64_t                    m_characterId;
	};

} // namespace engine::client
