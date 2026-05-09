#pragma once

#include "engine/core/Config.h"
#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server
{
	/// Supported trigger modes for one dynamic zone event.
	enum class DynamicEventTriggerType : uint8_t
	{
		Time = 1,
		Random = 2
	};

	/// One mob group spawned by an event phase using spawner-like data.
	struct DynamicEventSpawnDefinition
	{
		uint32_t archetypeId = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		uint32_t count = 1;
		float leashDistanceMeters = 24.0f;
	};

	/// One sequential phase belonging to a dynamic event.
	struct DynamicEventPhaseDefinition
	{
		std::string phaseId;
		std::string notificationText;
		uint32_t progressRequired = 1;
		std::vector<DynamicEventSpawnDefinition> spawns;
	};

	/// One reward bundle granted when the event finishes.
	struct DynamicEventReward
	{
		uint32_t experience = 0;
		uint32_t gold = 0;
		std::vector<ItemStack> items;
	};

	/// One dynamic event definition loaded from zone JSON content.
	struct DynamicEventDefinition
	{
		std::string eventId;
		uint32_t zoneId = 0;
		DynamicEventTriggerType triggerType = DynamicEventTriggerType::Time;
		uint32_t triggerSeconds = 1;
		uint32_t cooldownSeconds = 1;
		std::string startNotificationText;
		std::string completionNotificationText;
		std::vector<DynamicEventPhaseDefinition> phases;
		DynamicEventReward rewards;
	};

	/// Return a readable name for one dynamic event trigger type.
	const char* GetDynamicEventTriggerTypeName(DynamicEventTriggerType type);

	/// Server-side dynamic event runtime: JSON loading and schema validation.
	class EventRuntime final
	{
	public:
		/// Capture the config used to resolve zone event JSON files.
		explicit EventRuntime(const engine::core::Config& config);

		/// Emit shutdown logs when the event runtime is destroyed.
		~EventRuntime();

		/// Load every available zone event definition and validate the schema.
		bool Init();

		/// Release every loaded event definition and emit shutdown logs.
		void Shutdown();

		/// Return the loaded dynamic event definitions.
		const std::vector<DynamicEventDefinition>& GetDefinitions() const { return m_definitions; }

	private:
		/// Load event JSON files from the `zones/*/events.json` content locations.
		bool LoadDefinitions();

		engine::core::Config m_config;
		std::vector<DynamicEventDefinition> m_definitions;
		bool m_initialized = false;
	};
}
