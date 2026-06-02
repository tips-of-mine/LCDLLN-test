#include "src/client/dialogue/QuestConversationJournal.h"

#include "src/shared/core/Config.h"
#include "src/shared/platform/FileSystem.h"
#include "src/shared/core/Log.h"

#include <string>

namespace engine::client
{
	namespace
	{
		/// Échappe une chaîne pour insertion dans un littéral JSON.
		std::string JsonEscape(const std::string& s)
		{
			std::string out;
			out.reserve(s.size() + 8);
			for (char c : s)
			{
				switch (c)
				{
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n";  break;
					case '\r': out += "\\r";  break;
					case '\t': out += "\\t";  break;
					default:   out += c;      break;
				}
			}
			return out;
		}
	} // namespace

	QuestConversationJournal::QuestConversationJournal(const engine::core::Config& config,
	                                                   uint64_t characterId)
		: m_config(config), m_characterId(characterId)
	{
	}

	std::string QuestConversationJournal::RelPathForCharacter(uint64_t characterId)
	{
		return "quest_journal/character_" + std::to_string(characterId) + ".jsonl";
	}

	std::string QuestConversationJournal::SerializeEntryLine(const QuestConversationEntry& entry)
	{
		std::string out = "{";
		out += "\"npc\":\"" + JsonEscape(entry.npcLabel) + "\",";
		out += "\"questId\":" + std::to_string(entry.questId) + ",";
		out += "\"choice\":\"" + JsonEscape(entry.choiceText) + "\",";
		out += "\"lines\":[";
		for (size_t i = 0; i < entry.lines.size(); ++i)
		{
			if (i != 0) out += ",";
			out += "\"" + JsonEscape(entry.lines[i]) + "\"";
		}
		out += "]}";
		return out;
	}

	void QuestConversationJournal::RecordConversation(const QuestConversationEntry& entry)
	{
		const std::string relPath = RelPathForCharacter(m_characterId);

		// Append : on relit l'existant et on ajoute une ligne (JSONL).
		std::string content = engine::platform::FileSystem::ReadAllTextContent(m_config, relPath);
		if (!content.empty() && content.back() != '\n')
			content += "\n";
		content += SerializeEntryLine(entry);
		content += "\n";

		if (!engine::platform::FileSystem::WriteAllTextContent(m_config, relPath, content))
		{
			LOG_ERROR(Core, "[QuestConversationJournal] écriture impossible: '{}'", relPath);
			return;
		}
		LOG_INFO(Core, "[QuestConversationJournal] conversation consignée (quête {}, PNJ '{}')",
		         entry.questId, entry.npcLabel);
	}

} // namespace engine::client
