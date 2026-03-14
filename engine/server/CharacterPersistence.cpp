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
		outState.stats.currentHealth = static_cast<uint32_t>(persisted.GetInt("character.current_health", 100));
		outState.stats.maxHealth = static_cast<uint32_t>(persisted.GetInt("character.max_health", 100));
		outState.inventory.clear();

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

		LOG_INFO(Net,
			"[CharacterPersistence] Load OK (character_key={}, zone_id={}, inventory_items={})",
			characterKey,
			outState.zoneId,
			outState.inventory.size());
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
		output << "character.current_health=" << state.stats.currentHealth << "\n";
		output << "character.max_health=" << state.stats.maxHealth << "\n";
		output << "inventory.count=" << state.inventory.size() << "\n";
		for (size_t index = 0; index < state.inventory.size(); ++index)
		{
			output << "inventory." << index << ".item_id=" << state.inventory[index].itemId << "\n";
			output << "inventory." << index << ".quantity=" << state.inventory[index].quantity << "\n";
		}

		if (!engine::platform::FileSystem::WriteAllTextContent(m_config, relativePath, output.str()))
		{
			LOG_ERROR(Net, "[CharacterPersistence] Save FAILED (character_key={}, path={})",
				state.characterKey,
				relativePath);
			return false;
		}

		LOG_INFO(Net,
			"[CharacterPersistence] Save OK (character_key={}, zone_id={}, inventory_items={})",
			state.characterKey,
			state.zoneId,
			state.inventory.size());
		return true;
	}

	std::string CharacterPersistenceStore::BuildCharacterStateRelativePath(uint32_t characterKey) const
	{
		return "persistence/characters/character_" + std::to_string(characterKey) + ".ini";
	}
}
