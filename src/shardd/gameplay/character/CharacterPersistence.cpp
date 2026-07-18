#include "src/shardd/gameplay/character/CharacterPersistence.h"

#include "src/shardd/gameplay/character/CharacterPersistenceQuestCompat.h"
#include "src/shared/anniversary/CakeItemToken.h" // SP3 anniversaires (2026-07-18)
#include "src/shared/network/ServerProtocol.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <sstream>

namespace engine::server
{
	/// SP1 — Convertit une valeur de statut de quête persistée en QuestStatus.
	/// L'ancien format (formatVersion 0) sérialisait 0=Locked/1=Active/2=Completed ;
	/// le nouveau (formatVersion >= 1) sérialise directement l'enum QuestStatus (0..4).
	/// \param persistedValue valeur brute lue du fichier de personnage.
	/// \param formatVersion 0 = ancien schéma (0/1/2), >=1 = enum direct.
	/// \return QuestStatus mappé ; Locked pour toute valeur hors plage.
	QuestStatus MapPersistedQuestStatus(int64_t persistedValue, uint32_t formatVersion)
	{
		if (formatVersion == 0u)
		{
			switch (persistedValue)
			{
			case 0: return QuestStatus::Locked;
			case 1: return QuestStatus::Active;
			case 2: return QuestStatus::Completed;
			default: return QuestStatus::Locked;
			}
		}
		if (persistedValue >= 0 && persistedValue <= 4)
			return static_cast<QuestStatus>(static_cast<uint8_t>(persistedValue));
		return QuestStatus::Locked;
	}

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

	bool CharacterPersistenceStore::LoadCharacter(uint64_t characterKey, PersistedCharacterState& outState) const
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
		// Niveau : défaut 0 = absent du fichier (legacy) → l'enter-world préfère le niveau DB.
		outState.level = static_cast<uint32_t>(persisted.GetInt("character.level", 0));
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

		// Chantier 2 SP-A — équipement porté : clé plate equipment.<slot>.item_id.
		// Slots absents => 0 (rien d'équipé). Robuste aux fichiers legacy (pré-SP-A).
		outState.equipment.fill(0u);
		for (std::size_t slot = 1; slot < outState.equipment.size(); ++slot)
		{
			outState.equipment[slot] = static_cast<uint32_t>(
				persisted.GetInt("equipment." + std::to_string(slot) + ".item_id", 0));
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

			// SP1 — quests.format_version absent = personnages existants (schéma legacy).
			const uint32_t questFormatVersion =
				static_cast<uint32_t>(persisted.GetInt("quests.format_version", 0));
			const int64_t persistedStatus = persisted.GetInt("quests." + std::to_string(questIndex) + ".status", 0);
			questState.status = MapPersistedQuestStatus(persistedStatus, questFormatVersion);

			// EXT-2 — ms UTC de la dernière complétion. Absent des saves v1 (format_version < 2)
			// → défaut 0 : une quotidienne déjà complétée redeviendra disponible au 1er login
			// post-migration (comportement voulu, inoffensif).
			questState.completedAtEpochMs = static_cast<uint64_t>(
				persisted.GetInt("quests." + std::to_string(questIndex) + ".completed_at", 0));

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

		// Grimoire — 10 slots de barre d'action (absent = "" = vide).
		for (size_t slotIndex = 0; slotIndex < outState.actionBarLayout.size(); ++slotIndex)
		{
			outState.actionBarLayout[slotIndex] =
				persisted.GetString("actionbar.slot." + std::to_string(slotIndex), "");
		}

		// Anniversaires SP3 (2026-07-18) — expirations des gâteaux (clés
		// bornées : 10 ids possibles, cf. CakeItemToken). Absent/0 = pas
		// d'expiration enregistrée. Rétro-compatible : vieux fichiers sans
		// ces clés = map vide ; vieux lecteurs ignorent les clés inconnues.
		outState.cakeExpiresAtMsUtcByItemId.clear();
		for (uint32_t cakeId = engine::anniversary::kFirstCakeItemId;
			cakeId < engine::anniversary::kFirstCakeItemId + engine::anniversary::kCakeVariantCount;
			++cakeId)
		{
			const int64_t expiry = persisted.GetInt(
				"cake_expiry." + std::to_string(cakeId), 0);
			if (expiry > 0)
			{
				outState.cakeExpiresAtMsUtcByItemId[cakeId] = static_cast<uint64_t>(expiry);
			}
		}

	// SP-B — compétences par-classe déjà choisies.
	outState.knownSkillIds.clear();
	const uint32_t skillCount = static_cast<uint32_t>(persisted.GetInt("knownskill.count", 0));
	constexpr uint32_t kMaxKnownSkills = 60;
	const uint32_t maxSkills = std::min(skillCount, kMaxKnownSkills);
	for (uint32_t si = 0; si < maxSkills; ++si)
	{
		const std::string skillId = persisted.GetString("knownskill." + std::to_string(si), "");
		if (!skillId.empty())
		{
			outState.knownSkillIds.push_back(skillId);
		}
	}

	outState.mailboxGold = static_cast<uint32_t>(persisted.GetInt("mailbox.gold", 0));
	outState.mailboxItems.clear();
	const uint32_t mailboxItemCount = static_cast<uint32_t>(persisted.GetInt("mailbox.item_count", 0));
	constexpr uint32_t kMaxMailboxItems = 64;
	const uint32_t maxLoad = std::min(mailboxItemCount, kMaxMailboxItems);
	for (uint32_t mi = 0; mi < maxLoad; ++mi)
	{
		ItemStack m{};
		m.itemId = static_cast<uint32_t>(persisted.GetInt("mailbox.item." + std::to_string(mi) + ".id", 0));
		m.quantity = static_cast<uint32_t>(persisted.GetInt("mailbox.item." + std::to_string(mi) + ".qty", 0));
		if (m.itemId != 0u && m.quantity != 0u)
		{
			outState.mailboxItems.push_back(m);
		}
	}

	// M36.2 — Professions
	outState.professions.clear();
	const uint32_t professionCount = static_cast<uint32_t>(persisted.GetInt("professions.count", 0));
	constexpr uint32_t kMaxPersistedProfessions = 16;
	const uint32_t maxProf = std::min(professionCount, kMaxPersistedProfessions);
	for (uint32_t pi = 0; pi < maxProf; ++pi)
	{
		const std::string pKey = persisted.GetString("professions." + std::to_string(pi) + ".key", "");
		if (pKey.empty()) continue;
		ProfessionEntry entry{};
		entry.professionKey = pKey;
		entry.skillLevel    = static_cast<uint32_t>(persisted.GetInt("professions." + std::to_string(pi) + ".skill", 1));
		entry.isPrimary     = persisted.GetBool("professions." + std::to_string(pi) + ".primary", false);
		if (entry.skillLevel < 1) entry.skillLevel = 1;
		if (entry.skillLevel > kMaxProfessionSkillLevel) entry.skillLevel = kMaxProfessionSkillLevel;
		outState.professions.push_back(std::move(entry));
	}

	LOG_INFO(Net,
		"[CharacterPersistence] Load OK (character_key={}, zone_id={}, inventory_items={}, quests={}, chat_ignore={}, moderator={}, mailbox_gold={}, mailbox_items={}, professions={})",
		characterKey,
		outState.zoneId,
		outState.inventory.size(),
		outState.questStates.size(),
		outState.chatIgnoredDisplayNames.size(),
		outState.chatModeratorRole ? "true" : "false",
		outState.mailboxGold,
		outState.mailboxItems.size(),
		outState.professions.size());
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
		output << "character.level=" << state.level << "\n";
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
		// Chantier 2 SP-A — équipement porté (n'écrit que les slots occupés).
		for (std::size_t slot = 1; slot < state.equipment.size(); ++slot)
		{
			if (state.equipment[slot] != 0u)
			{
				output << "equipment." << slot << ".item_id=" << state.equipment[slot] << "\n";
			}
		}
		// SP1 — format_version=1 : quests.*.status écrit l'enum QuestStatus direct (0..4).
		// EXT-2 — format_version=2 : ajoute quests.<i>.completed_at (ms UTC de la dernière
		// complétion) ; bump additif, les saves v1 (sans la clé) chargent completed_at=0.
		output << "quests.format_version=2\n";
		output << "quests.count=" << state.questStates.size() << "\n";
		for (size_t questIndex = 0; questIndex < state.questStates.size(); ++questIndex)
		{
			output << "quests." << questIndex << ".id=" << state.questStates[questIndex].questId << "\n";
			output << "quests." << questIndex << ".status=" << static_cast<uint32_t>(state.questStates[questIndex].status) << "\n";
			output << "quests." << questIndex << ".completed_at=" << state.questStates[questIndex].completedAtEpochMs << "\n";
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

		// Grimoire — 10 slots de barre d'action (clés fixes, "" = slot vide ;
		// l'alignement positionnel est conservé, contrairement à chat.ignore).
		for (size_t slotIndex = 0; slotIndex < state.actionBarLayout.size(); ++slotIndex)
		{
			output << "actionbar.slot." << slotIndex << "=" << state.actionBarLayout[slotIndex] << "\n";
		}

		// Anniversaires SP3 (2026-07-18) — expirations UTC (epoch ms) des
		// gâteaux possédés (clés par itemId, cf. CakeItemToken).
		for (const auto& [cakeItemId, expiresAtMs] : state.cakeExpiresAtMsUtcByItemId)
		{
			output << "cake_expiry." << cakeItemId << "=" << expiresAtMs << "\n";
		}

	// SP-B — compétences par-classe déjà choisies (un skill par tier/niveau débloqué).
	const size_t skillsToSave = std::min<size_t>(state.knownSkillIds.size(), 60u);
	output << "knownskill.count=" << skillsToSave << "\n";
	for (size_t si = 0; si < skillsToSave; ++si)
	{
		output << "knownskill." << si << "=" << state.knownSkillIds[si] << "\n";
	}

	output << "mailbox.gold=" << state.mailboxGold << "\n";
	const size_t mailboxToSave = std::min<size_t>(state.mailboxItems.size(), 64u);
	output << "mailbox.item_count=" << mailboxToSave << "\n";
	for (size_t mi = 0; mi < mailboxToSave; ++mi)
	{
		output << "mailbox.item." << mi << ".id=" << state.mailboxItems[mi].itemId << "\n";
		output << "mailbox.item." << mi << ".qty=" << state.mailboxItems[mi].quantity << "\n";
	}

	// M36.2 — Professions
	const size_t profToSave = std::min<size_t>(state.professions.size(), 16u);
	output << "professions.count=" << profToSave << "\n";
	for (size_t pi = 0; pi < profToSave; ++pi)
	{
		output << "professions." << pi << ".key=" << state.professions[pi].professionKey << "\n";
		output << "professions." << pi << ".skill=" << state.professions[pi].skillLevel << "\n";
		output << "professions." << pi << ".primary=" << (state.professions[pi].isPrimary ? 1 : 0) << "\n";
	}

	if (!engine::platform::FileSystem::WriteAllTextContent(m_config, relativePath, output.str()))
		{
			LOG_ERROR(Net, "[CharacterPersistence] Save FAILED (character_key={}, path={})",
				state.characterKey,
				relativePath);
			return false;
		}

		LOG_INFO(Net,
			"[CharacterPersistence] Save OK (character_key={}, zone_id={}, inventory_items={}, quests={}, chat_ignore={}, mailbox_gold={}, mailbox_items={})",
			state.characterKey,
			state.zoneId,
			state.inventory.size(),
			state.questStates.size(),
			ignoreCountToSave,
			state.mailboxGold,
			mailboxToSave);
		return true;
	}

	std::string CharacterPersistenceStore::BuildCharacterStateRelativePath(uint64_t characterKey) const
	{
		return "persistence/characters/character_" + std::to_string(characterKey) + ".ini";
	}
}
