#pragma once

#include "engine/core/Config.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::server
{
	/// One data-driven mob spawner loaded from zone content.
	struct SpawnerDefinition
	{
		std::string spawnerId;
		uint32_t zoneId = 0;
		uint32_t archetypeId = 0;
		float positionMetersX = 0.0f;
		float positionMetersY = 0.0f;
		float positionMetersZ = 0.0f;
		uint32_t count = 1;
		uint32_t respawnSeconds = 1;
		float leashDistanceMeters = 24.0f;
	};

	/// Server-side spawner runtime: loads zone JSON definitions resolved from `paths.content`.
	class SpawnerRuntime final
	{
	public:
		/// Capture the config used to resolve zone spawner JSON files.
		explicit SpawnerRuntime(const engine::core::Config& config);

		/// Emit shutdown logs when the spawner runtime is destroyed.
		~SpawnerRuntime();

		/// Load every available zone spawner definition and validate the schema.
		bool Init();

		/// Release every loaded spawner definition and emit shutdown logs.
		void Shutdown();

		/// Return the loaded spawner definitions.
		const std::vector<SpawnerDefinition>& GetDefinitions() const { return m_definitions; }

	private:
		/// Load spawner JSON files from the `zones/*/spawners.json` content locations.
		bool LoadDefinitions();

		engine::core::Config m_config;
		std::vector<SpawnerDefinition> m_definitions;
		bool m_initialized = false;
	};
}
