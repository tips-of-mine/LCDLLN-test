#pragma once

#include "engine/client/UIModel.h"
#include "engine/core/Config.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::client
{
	/// One data-driven FX definition loaded from `game/data/fx/effects.json`.
	struct FXDefinition
	{
		std::string id;
		std::string particleId;
		std::string decalId;
		std::string soundId;
		float scale = 1.0f;
		float durationSeconds = 0.0f;
	};

	/// One event kind to FX id mapping loaded from `game/data/fx/event_mappings.json`.
	struct FXEventMapping
	{
		std::string eventType;
		std::string fxId;
	};

	/// One spawned particle request emitted by the FX manager.
	struct SpawnedParticleFx
	{
		uint64_t sequence = 0;
		std::string fxId;
		std::string particleId;
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		float scale = 1.0f;
		float remainingSeconds = 0.0f;
	};

	/// One spawned decal request emitted by the FX manager.
	struct SpawnedDecalFx
	{
		uint64_t sequence = 0;
		std::string fxId;
		std::string decalId;
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		float scale = 1.0f;
		float remainingSeconds = 0.0f;
	};

	/// One spawned sound request emitted by the FX manager.
	struct SpawnedSoundFx
	{
		uint64_t sequence = 0;
		std::string fxId;
		std::string soundId;
		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		float remainingSeconds = 0.0f;
	};

	/// Client-side runtime mapping network events to data-driven FX spawn requests.
	class FXManager final
	{
	public:
		/// Construct an uninitialized FX manager.
		FXManager() = default;

		/// Release loaded FX definitions and active requests.
		~FXManager();

		/// Initialize the manager and load content-relative FX definitions/mappings.
		bool Init(const engine::core::Config& config);

		/// Shutdown the manager and clear all active FX requests.
		void Shutdown();

		/// Apply one network packet to the FX runtime using the latest UI model state.
		bool ApplyPacket(std::span<const std::byte> packet, const UIModel& model);

		/// Advance FX lifetimes and remove expired requests.
		bool Tick(float deltaSeconds);

		/// Read-only access to the active particle requests.
		const std::vector<SpawnedParticleFx>& GetParticles() const { return m_particles; }

		/// Read-only access to the active decal requests.
		const std::vector<SpawnedDecalFx>& GetDecals() const { return m_decals; }

		/// Read-only access to the active sound requests.
		const std::vector<SpawnedSoundFx>& GetSounds() const { return m_sounds; }

	private:
		/// Load `effects.json` from the configured content-relative path.
		bool LoadDefinitions();

		/// Load `event_mappings.json` from the configured content-relative path.
		bool LoadMappings();

		/// Spawn one configured FX payload for the given world-space position.
		bool SpawnFxById(std::string_view fxId, float positionX, float positionY, float positionZ);

		/// Handle one decoded combat event and map it to a configured FX id.
		bool ApplyCombatEvent(std::span<const std::byte> packet, const UIModel& model);

		/// Resolve the world-space hit position from the current UI model and combat event.
		bool ResolveCombatFxPosition(const engine::server::CombatEventMessage& message, const UIModel& model, float& outX, float& outY, float& outZ) const;

		/// Remove expired FX requests from one request list.
		template <typename TFx>
		void RemoveExpired(std::vector<TFx>& entries);

		engine::core::Config m_config{};
		std::unordered_map<std::string, FXDefinition> m_definitions;
		std::unordered_map<std::string, std::string> m_eventToFx;
		std::vector<SpawnedParticleFx> m_particles;
		std::vector<SpawnedDecalFx> m_decals;
		std::vector<SpawnedSoundFx> m_sounds;
		engine::server::CombatEventMessage m_combatEventMessage{};
		std::string m_effectsRelativePath;
		std::string m_mappingsRelativePath;
		uint64_t m_nextSequence = 1;
		bool m_initialized = false;
	};
}
