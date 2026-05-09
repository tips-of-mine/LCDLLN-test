#pragma once

#include "engine/core/Config.h"
#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server
{
	/// Supported quest step event types loaded from the JSON definitions.
	enum class QuestStepType : uint8_t
	{
		Kill = 1,
		Collect = 2,
		Talk = 3,
		Enter = 4
	};

	/// Runtime status stored for one quest on one player.
	enum class QuestStatus : uint8_t
	{
		Locked = 0,
		Active = 1,
		Completed = 2
	};

	/// One typed step loaded from the quest definitions JSON.
	struct QuestStepDefinition
	{
		QuestStepType type = QuestStepType::Kill;
		std::string targetId;
		uint32_t requiredCount = 1;
	};

	/// One reward bundle granted when a quest is completed.
	struct QuestReward
	{
		uint32_t experience = 0;
		uint32_t gold = 0;
		std::vector<ItemStack> items;
	};

	/// One quest definition loaded from the data-driven JSON file.
	struct QuestDefinition
	{
		std::string questId;
		std::vector<std::string> prerequisiteQuestIds;
		std::vector<QuestStepDefinition> steps;
		QuestReward rewards;
	};

	/// Per-player stored quest state required by the server runtime.
	struct QuestState
	{
		std::string questId;
		QuestStatus status = QuestStatus::Locked;
		std::vector<uint32_t> stepProgressCounts;
	};

	/// One quest state change emitted after sync or progress evaluation.
	struct QuestProgressDelta
	{
		std::string questId;
		QuestStatus status = QuestStatus::Locked;
		std::vector<uint32_t> stepProgressCounts;
		uint32_t rewardExperience = 0;
		uint32_t rewardGold = 0;
		std::vector<ItemStack> rewardItems;
	};

	/// Return a readable name for one quest step type.
	const char* GetQuestStepTypeName(QuestStepType type);

	/// Return a readable name for one quest status.
	const char* GetQuestStatusName(QuestStatus status);

	/// Server-side quest runtime: JSON loading, prerequisite sync and progress evaluation.
	class QuestRuntime final
	{
	public:
		/// Capture the config used to resolve the quest JSON content path.
		explicit QuestRuntime(const engine::core::Config& config);

		/// Emit shutdown logs when the quest runtime is destroyed.
		~QuestRuntime();

		/// Load the JSON quest definitions and validate the schema.
		bool Init();

		/// Release every loaded quest definition and emit shutdown logs.
		void Shutdown();

		/// Ensure one player's quest state table matches the loaded quest definitions.
		bool SyncQuestStates(std::vector<QuestState>& states, std::vector<QuestProgressDelta>& outDeltas) const;

		/// Apply one authoritative quest event and emit deltas for every changed quest.
		bool ApplyEvent(
			std::vector<QuestState>& states,
			QuestStepType eventType,
			std::string_view targetId,
			uint32_t amount,
			std::vector<QuestProgressDelta>& outDeltas) const;

		/// Find one quest definition by id, or return `nullptr` when it is absent.
		const QuestDefinition* FindQuestDefinition(std::string_view questId) const;

	private:
		/// Load every quest definition from the configured JSON content file.
		bool LoadDefinitions();

		engine::core::Config m_config;
		std::string m_questDefinitionsRelativePath;
		std::vector<QuestDefinition> m_definitions;
		bool m_initialized = false;
	};
}
