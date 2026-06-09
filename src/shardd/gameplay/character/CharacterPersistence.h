#pragma once

#include "src/shared/core/Config.h"
#include "src/shardd/gameplay/crafting/CraftingSystem.h"
#include "src/shardd/gameplay/quest/QuestRuntime.h"
#include "src/shared/network/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
	/// Persisted character payload stored by the server save/load pipeline.
	struct PersistedCharacterState
	{
		/// Phase 3.7.5 — élargi à uint64 (character_id BIGINT UNSIGNED, valeur Hello).
		uint64_t characterKey = 0;
		uint32_t zoneId = 1;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		uint32_t experiencePoints = 0;
		/// Niveau du personnage. 0 = absent du fichier (legacy / non encore écrit) →
		/// l'enter-world retombe sur le niveau de la DB. Renseigné par les level-ups runtime.
		uint32_t level = 0;
		uint32_t gold = 0;
		/// M35.1 — additional wallet currencies (mirrors MySQL player_wallet when present).
		uint32_t honor = 0;
		uint32_t badges = 0;
		uint32_t premiumCurrency = 0;
		StatsComponent stats{};
		std::vector<ItemStack> inventory;
		std::vector<QuestState> questStates;
		/// M29.2: persisted `/ignore` targets (display names, e.g. P12).
		std::vector<std::string> chatIgnoredDisplayNames;
		/// M29.2: when true, client may use admin chat commands (`/kick`, `/ban`, …).
		bool chatModeratorRole = false;
		/// M35.4 — gold returned by mail when outbid/offline auction delivery; merged on login.
		uint32_t mailboxGold = 0;
		/// M35.4 — items attached to mailbox (e.g. won auctions while offline).
		std::vector<ItemStack> mailboxItems;
		/// M36.2 — known professions and skill levels (persisted per character).
		std::vector<ProfessionEntry> professions;
	};

	/// Minimal file-backed character persistence store used by the server runtime.
	class CharacterPersistenceStore final
	{
	public:
		/// Capture the config used to resolve schema and save paths.
		explicit CharacterPersistenceStore(const engine::core::Config& config);

		/// Emit shutdown logs when the persistence store is destroyed.
		~CharacterPersistenceStore();

		/// Validate the configured schema/migration file before the server starts.
		bool Init();

		/// Release persistence resources and emit shutdown logs.
		void Shutdown();

		/// Load one character state if it was previously persisted.
		bool LoadCharacter(uint64_t characterKey, PersistedCharacterState& outState) const;

		/// Save one character state to the configured persistence location.
		bool SaveCharacter(const PersistedCharacterState& state) const;

	private:
		/// Build the relative content path used to store one character state file.
		std::string BuildCharacterStateRelativePath(uint64_t characterKey) const;

		engine::core::Config m_config;
		std::string m_schemaRelativePath;
		bool m_initialized = false;
	};
}
