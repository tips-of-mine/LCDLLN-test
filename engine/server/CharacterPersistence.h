#pragma once

#include "engine/core/Config.h"
#include "engine/server/QuestRuntime.h"
#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::server
{
	/// Persisted character payload stored by the server save/load pipeline.
	struct PersistedCharacterState
	{
		uint32_t characterKey = 0;
		uint32_t zoneId = 1;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		uint32_t experiencePoints = 0;
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
		bool LoadCharacter(uint32_t characterKey, PersistedCharacterState& outState) const;

		/// Save one character state to the configured persistence location.
		bool SaveCharacter(const PersistedCharacterState& state) const;

	private:
		/// Build the relative content path used to store one character state file.
		std::string BuildCharacterStateRelativePath(uint32_t characterKey) const;

		engine::core::Config m_config;
		std::string m_schemaRelativePath;
		bool m_initialized = false;
	};
}
