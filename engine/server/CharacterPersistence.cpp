#include "engine/server/CharacterPersistence.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <sstream>

namespace engine::server
{
	CharacterPersistenceStore::CharacterPersistenceStore(const engine::core::Config& config)
		: m_config(config)
		, m_schemaRelativePath(m_config.GetString(
			"server.persistence_schema_path",
			"persistence/db/migrations/0001_characters_inventory.sql"))
	{
		LOG_INFO(Net, "[CharacterPersistence] Constructed");
	}

	CharacterPersistenceStore::~CharacterPersistenceStore()
	{
		Shutdown();
	}

	bool CharacterPersistenceStore::Init()
	{
		if (m_initialized)
		{
			LOG_WARN(Net, "[CharacterPersistence] Init ignored: already initialized");
			return true;
		}

		const auto schemaPath = engine::platform::FileSystem::ResolveContentPath(m_config, m_schemaRelativePath);
		if (!engine::platform::FileSystem::Exists(schemaPath))
		{
			LOG_ERROR(Net, "[CharacterPersistence] Init FAILED: schema file missing ({})", m_schemaRelativePath);
			return false;
		}

		m_initialized = true;
		LOG_INFO(Net, "[CharacterPersistence] Init OK (schema_path={})", m_schemaRelativePath);
		return true;
	}

	void CharacterPersistenceStore::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		LOG_INFO(Net, "[CharacterPersistence] Destroyed");
	}

	bool CharacterPersistenceStore::LoadCharacter(uint32_t characterKey, PersistedCharacterState& outState) const
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[CharacterPersistence] Load ignored: store not initialized");
			return false;
		}

		const std::string relativePath = BuildCharacterStateRelativePath(characterKey);
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(m_config, relativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_WARN(Net, "[CharacterPersistence] Load skipped: character file missing (character_key={}, path={})",
				characterKey,
				relativePath);
			return false;
		}

		engine::core::Config persisted;
		if (!persisted.LoadFromFile(fullPath.string()))
		{
			LOG_ERROR(Net, "[CharacterPersistence] Load FAILED: invalid persistence file (character_key={}, path={})",
				characterKey,
				relativePath);
			return false;
		}

		outState.characterKey = characterKey;
		outState.zoneId = static_cast<uint32_t>(persisted.GetInt("character.zone_id", 1));
		outState.positionMetersX = static_cast<float>(persisted.GetDouble("character.position_x", 0.0));
		outState.positionMetersY = static_cast<float>(persisted.GetDouble("character.position_y", 0.0));
		outState.positionMetersZ = static_cast<float>(persisted.GetDouble("character.position_z", 0.0));
		outState.experiencePoints = static_cast<uint32_t>(persisted.GetInt("character.experience_points", 0));
		outState.gold = static_cast<uint32_t>(persisted.GetInt("character.gold", 0));
		outState.honor = static_cast<uint32_t>(persisted.GetInt("character.honor", 0));
		outState.badges = static_cast<uint32_t>(persisted.GetInt("character.badges", 0));
		outState.premiumCurrency = static_cast<uint32_t>(persisted.GetInt("character.premium_currency", 0));
		outState.stats.currentHealth = static_cast<uint32_t>(persisted.GetInt("character.current_health", 100));
		outState.stats.maxHealth = static_cast<uint32_t>(persisted.GetInt("character.max_health", 100));
		outState.inventory.clear();
		outState.questStates.clear();

		const uint32_t inventoryCount = static_cast<uint32_t>(persisted.GetInt("inventory.count", 0));
		for (uint32_t index = 0; index < inventoryCount; ++index)
		{
			ItemStack item{};
			item.itemId = static_cast<uint32_t>(persisted.GetInt("inventory." + std::to_string(index) + ".item_id", 0));
			item.quantity = static_cast<uint32_t>(persisted.GetInt("inventory." + std::to_string(index) + ".quantity", 0));
			if (item.itemId == 0 || item.quantity == 0)
			{
				continue;
			}

			outState.inventory.push_back(item);
		}

		const uint32_t questCount = static_cast<uint32_t>(persisted.GetInt("quests.count", 0));
		for (uint32_t questIndex = 0; questIndex < questCount; ++questIndex)
		{
			QuestState questState{};
			questState.questId = persisted.GetString("quests." + std::to_string(questIndex) + ".id", "");
			if (questState.questId.empty())
			{
				continue;
			}

			const int64_t persistedStatus = persisted.GetInt("quests." + std::to_string(questIndex) + ".status", 0);
			if (persistedStatus <= 0)
			{
				questState.status = QuestStatus::Locked;
			}
			else if (persistedStatus == 1)
			{
				questState.status = QuestStatus::Active;
			}
			else
			{
				questState.status = QuestStatus::Completed;
			}

			const uint32_t stepCount = static_cast<uint32_t>(persisted.GetInt("quests." + std::to_string(questIndex) + ".step_count", 0));
			questState.stepProgressCounts.reserve(stepCount);
			for (uint32_t stepIndex = 0; stepIndex < stepCount; ++stepIndex)
			{
				questState.stepProgressCounts.push_back(
					static_cast<uint32_t>(persisted.GetInt(
						"quests." + std::to_string(questIndex) + ".step." + std::to_string(stepIndex) + ".progress",
						0)));
			}

			outState.questStates.push_back(std::move(questState));
		}

		outState.chatModeratorRole = persisted.GetBool("character.chat_moderator_role", false);
		outState.chatIgnoredDisplayNames.clear();
		const uint32_t ignoreCount = static_cast<uint32_t>(persisted.GetInt("chat.ignore.count", 0));
		constexpr uint32_t kMaxIgnoredChatNames = 32;
		for (uint32_t ignoreIndex = 0; ignoreIndex < ignoreCount && ignoreIndex < kMaxIgnoredChatNames; ++ignoreIndex)
		{
			const std::string entry = persisted.GetString("chat.ignore." + std::to_string(ignoreIndex) + ".name", "");
			if (!entry.empty())
			{
				outState.chatIgnoredDisplayNames.push_back(entry);
			}
		}

		LOG_INFO(Net,
			"[CharacterPersistence] Load OK (character_key={}, zone_id={}, inventory_items={}, quests={}, chat_ignore={}, moderator={})",
			characterKey,
			outState.zoneId,
			outState.inventory.size(),
			outState.questStates.size(),
			outState.chatIgnoredDisplayNames.size(),
			outState.chatModeratorRole ? "true" : "false");
		return true;
	}

	bool CharacterPersistenceStore::SaveCharacter(const PersistedCharacterState& state) const
	{
		if (!m_initialized)
		{
			LOG_WARN(Net, "[CharacterPersistence] Save ignored: store not initialized");
			return false;
		}

		const std::string relativePath = BuildCharacterStateRelativePath(state.characterKey);
		std::ostringstream output;
		output << "character.zone_id=" << state.zoneId << "\n";
		output << "character.position_x=" << state.positionMetersX << "\n";
		output << "character.position_y=" << state.positionMetersY << "\n";
		output << "character.position_z=" << state.positionMetersZ << "\n";
		output << "character.experience_points=" << state.experiencePoints << "\n";
		output << "character.gold=" << state.gold << "\n";
		output << "character.honor=" << state.honor << "\n";
		output << "character.badges=" << state.badges << "\n";
		output << "character.premium_currency=" << state.premiumCurrency << "\n";
		output << "character.current_health=" << state.stats.currentHealth << "\n";
		output << "character.max_health=" << state.stats.maxHealth << "\n";
		output << "character.chat_moderator_role=" << (state.chatModeratorRole ? 1 : 0) << "\n";
		output << "inventory.count=" << state.inventory.size() << "\n";
		for (size_t index = 0; index < state.inventory.size(); ++index)
		{
			output << "inventory." << index << ".item_id=" << state.inventory[index].itemId << "\n";
			output << "inventory." << index << ".quantity=" << state.inventory[index].quantity << "\n";
		}
		output << "quests.count=" << state.questStates.size() << "\n";
		for (size_t questIndex = 0; questIndex < state.questStates.size(); ++questIndex)
		{
			output << "quests." << questIndex << ".id=" << state.questStates[questIndex].questId << "\n";
			output << "quests." << questIndex << ".status=" << static_cast<uint32_t>(state.questStates[questIndex].status) << "\n";
			output << "quests." << questIndex << ".step_count=" << state.questStates[questIndex].stepProgressCounts.size() << "\n";
			for (size_t stepIndex = 0; stepIndex < state.questStates[questIndex].stepProgressCounts.size(); ++stepIndex)
			{
				output << "quests." << questIndex << ".step." << stepIndex << ".progress="
					<< state.questStates[questIndex].stepProgressCounts[stepIndex] << "\n";
			}
		}

		const size_t ignoreCountToSave = std::min<size_t>(state.chatIgnoredDisplayNames.size(), 32u);
		output << "chat.ignore.count=" << ignoreCountToSave << "\n";
		for (size_t ignoreIndex = 0; ignoreIndex < ignoreCountToSave; ++ignoreIndex)
		{
			output << "chat.ignore." << ignoreIndex << ".name=" << state.chatIgnoredDisplayNames[ignoreIndex] << "\n";
		}

		if (!engine::platform::FileSystem::WriteAllTextContent(m_config, relativePath, output.str()))
		{
			LOG_ERROR(Net, "[CharacterPersistence] Save FAILED (character_key={}, path={})",
				state.characterKey,
				relativePath);
			return false;
		}

		LOG_INFO(Net,
			"[CharacterPersistence] Save OK (character_key={}, zone_id={}, inventory_items={}, quests={}, chat_ignore={})",
			state.characterKey,
			state.zoneId,
			state.inventory.size(),
			state.questStates.size(),
			ignoreCountToSave);
		return true;
	}

	std::string CharacterPersistenceStore::BuildCharacterStateRelativePath(uint32_t characterKey) const
	{
		return "persistence/characters/character_" + std::to_string(characterKey) + ".ini";
	}
}
