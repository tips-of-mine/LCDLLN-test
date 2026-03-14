#include "engine/client/FXManager.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"
#include "engine/server/ServerProtocol.h"

#include <algorithm>

namespace engine::client
{
	namespace
	{
		/// Return true when the flattened config contains one indexed key.
		bool HasIndexedKey(const engine::core::Config& config, std::string_view baseKey, size_t index, std::string_view field)
		{
			const std::string key = std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return config.Has(key);
		}

		/// Return one string field from a flattened indexed config object.
		std::string GetIndexedString(const engine::core::Config& config, std::string_view baseKey, size_t index, std::string_view field)
		{
			const std::string key = std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return config.GetString(key);
		}

		/// Return one double field from a flattened indexed config object.
		double GetIndexedDouble(const engine::core::Config& config, std::string_view baseKey, size_t index, std::string_view field, double fallback)
		{
			const std::string key = std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return config.GetDouble(key, fallback);
		}
	}

	FXManager::~FXManager()
	{
		Shutdown();
	}

	bool FXManager::Init(const engine::core::Config& config)
	{
		if (m_initialized)
		{
			LOG_WARN(Core, "[FXManager] Init ignored: already initialized");
			return true;
		}

		m_config = config;
		m_effectsRelativePath = m_config.GetString("fx.effects_path", "fx/effects.json");
		m_mappingsRelativePath = m_config.GetString("fx.event_mappings_path", "fx/event_mappings.json");
		if (!LoadDefinitions())
		{
			LOG_ERROR(Core, "[FXManager] Init FAILED: definition load failed ({})", m_effectsRelativePath);
			return false;
		}
		if (!LoadMappings())
		{
			LOG_ERROR(Core, "[FXManager] Init FAILED: mapping load failed ({})", m_mappingsRelativePath);
			m_definitions.clear();
			return false;
		}

		m_initialized = true;
		LOG_INFO(Core, "[FXManager] Init OK (definitions={}, mappings={})", m_definitions.size(), m_eventToFx.size());
		return true;
	}

	void FXManager::Shutdown()
	{
		if (!m_initialized)
		{
			return;
		}

		m_initialized = false;
		m_definitions.clear();
		m_eventToFx.clear();
		m_particles.clear();
		m_decals.clear();
		m_sounds.clear();
		m_effectsRelativePath.clear();
		m_mappingsRelativePath.clear();
		m_nextSequence = 1;
		LOG_INFO(Core, "[FXManager] Destroyed");
	}

	bool FXManager::ApplyPacket(std::span<const std::byte> packet, const UIModel& model)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[FXManager] ApplyPacket FAILED: manager not initialized");
			return false;
		}

		engine::server::MessageKind kind{};
		if (!engine::server::PeekMessageKind(packet, kind))
		{
			LOG_WARN(Core, "[FXManager] ApplyPacket FAILED: invalid packet header");
			return false;
		}

		switch (kind)
		{
		case engine::server::MessageKind::CombatEvent:
			return ApplyCombatEvent(packet, model);
		default:
			LOG_DEBUG(Core, "[FXManager] ApplyPacket ignored: unsupported message kind {}", static_cast<uint16_t>(kind));
			return true;
		}
	}

	bool FXManager::Tick(float deltaSeconds)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Core, "[FXManager] Tick FAILED: manager not initialized");
			return false;
		}

		if (deltaSeconds < 0.0f)
		{
			LOG_WARN(Core, "[FXManager] Tick FAILED: negative delta {}", deltaSeconds);
			return false;
		}

		for (SpawnedParticleFx& fx : m_particles)
		{
			fx.remainingSeconds = std::max(0.0f, fx.remainingSeconds - deltaSeconds);
		}
		for (SpawnedDecalFx& fx : m_decals)
		{
			fx.remainingSeconds = std::max(0.0f, fx.remainingSeconds - deltaSeconds);
		}
		for (SpawnedSoundFx& fx : m_sounds)
		{
			fx.remainingSeconds = std::max(0.0f, fx.remainingSeconds - deltaSeconds);
		}

		RemoveExpired(m_particles);
		RemoveExpired(m_decals);
		RemoveExpired(m_sounds);
		return true;
	}

	bool FXManager::LoadDefinitions()
	{
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(m_config, m_effectsRelativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_ERROR(Core, "[FXManager] Definition load FAILED: missing file {}", m_effectsRelativePath);
			return false;
		}

		engine::core::Config fxConfig;
		if (!fxConfig.LoadFromFile(fullPath.string()))
		{
			LOG_ERROR(Core, "[FXManager] Definition load FAILED: invalid json {}", m_effectsRelativePath);
			return false;
		}

		m_definitions.clear();
		for (size_t index = 0; HasIndexedKey(fxConfig, "effects", index, "id"); ++index)
		{
			FXDefinition definition{};
			definition.id = GetIndexedString(fxConfig, "effects", index, "id");
			definition.particleId = GetIndexedString(fxConfig, "effects", index, "particleId");
			definition.decalId = GetIndexedString(fxConfig, "effects", index, "decalId");
			definition.soundId = GetIndexedString(fxConfig, "effects", index, "soundId");
			definition.scale = static_cast<float>(GetIndexedDouble(fxConfig, "effects", index, "scale", 1.0));
			definition.durationSeconds = static_cast<float>(GetIndexedDouble(fxConfig, "effects", index, "durationSeconds", 0.0));
			if (definition.id.empty())
			{
				LOG_WARN(Core, "[FXManager] Definition ignored: empty id at effects[{}]", index);
				continue;
			}

			m_definitions.insert_or_assign(definition.id, definition);
			LOG_INFO(Core, "[FXManager] FX definition loaded (id={}, particle={}, decal={}, sound={})",
				definition.id,
				definition.particleId,
				definition.decalId,
				definition.soundId);
		}

		if (m_definitions.empty())
		{
			LOG_ERROR(Core, "[FXManager] Definition load FAILED: no valid effects ({})", m_effectsRelativePath);
			return false;
		}

		return true;
	}

	bool FXManager::LoadMappings()
	{
		const auto fullPath = engine::platform::FileSystem::ResolveContentPath(m_config, m_mappingsRelativePath);
		if (!engine::platform::FileSystem::Exists(fullPath))
		{
			LOG_ERROR(Core, "[FXManager] Mapping load FAILED: missing file {}", m_mappingsRelativePath);
			return false;
		}

		engine::core::Config mappingConfig;
		if (!mappingConfig.LoadFromFile(fullPath.string()))
		{
			LOG_ERROR(Core, "[FXManager] Mapping load FAILED: invalid json {}", m_mappingsRelativePath);
			return false;
		}

		m_eventToFx.clear();
		for (size_t index = 0; HasIndexedKey(mappingConfig, "mappings", index, "eventType"); ++index)
		{
			const std::string eventType = GetIndexedString(mappingConfig, "mappings", index, "eventType");
			const std::string fxId = GetIndexedString(mappingConfig, "mappings", index, "fxId");
			if (eventType.empty() || fxId.empty())
			{
				LOG_WARN(Core, "[FXManager] Mapping ignored: incomplete entry at mappings[{}]", index);
				continue;
			}

			m_eventToFx.insert_or_assign(eventType, fxId);
			LOG_INFO(Core, "[FXManager] FX mapping loaded (event_type={}, fx_id={})", eventType, fxId);
		}

		if (m_eventToFx.empty())
		{
			LOG_ERROR(Core, "[FXManager] Mapping load FAILED: no valid mappings ({})", m_mappingsRelativePath);
			return false;
		}

		return true;
	}

	bool FXManager::SpawnFxById(std::string_view fxId, float positionX, float positionY, float positionZ)
	{
		const auto it = m_definitions.find(std::string(fxId));
		if (it == m_definitions.end())
		{
			LOG_WARN(Core, "[FXManager] Spawn FAILED: unknown fx id {}", fxId);
			return false;
		}

		const FXDefinition& definition = it->second;
		const uint64_t sequence = m_nextSequence++;
		if (!definition.particleId.empty())
		{
			m_particles.push_back(SpawnedParticleFx{
				sequence,
				definition.id,
				definition.particleId,
				positionX,
				positionY,
				positionZ,
				definition.scale,
				definition.durationSeconds
			});
		}
		if (!definition.decalId.empty())
		{
			m_decals.push_back(SpawnedDecalFx{
				sequence,
				definition.id,
				definition.decalId,
				positionX,
				positionY,
				positionZ,
				definition.scale,
				definition.durationSeconds
			});
		}
		if (!definition.soundId.empty())
		{
			m_sounds.push_back(SpawnedSoundFx{
				sequence,
				definition.id,
				definition.soundId,
				positionX,
				positionY,
				positionZ,
				definition.durationSeconds
			});
		}

		LOG_INFO(Core, "[FXManager] FX spawned (fx_id={}, pos=({:.2f}, {:.2f}, {:.2f}), particle={}, decal={}, sound={})",
			definition.id,
			positionX,
			positionY,
			positionZ,
			definition.particleId,
			definition.decalId,
			definition.soundId);
		return true;
	}

	bool FXManager::ApplyCombatEvent(std::span<const std::byte> packet, const UIModel& model)
	{
		if (!engine::server::DecodeCombatEvent(packet, m_combatEventMessage))
		{
			LOG_WARN(Core, "[FXManager] CombatEvent FAILED: decode error");
			return false;
		}

		const auto mappingIt = m_eventToFx.find("CombatEvent");
		if (mappingIt == m_eventToFx.end())
		{
			LOG_WARN(Core, "[FXManager] CombatEvent ignored: no FX mapping");
			return false;
		}

		float positionX = 0.0f;
		float positionY = 0.0f;
		float positionZ = 0.0f;
		if (!ResolveCombatFxPosition(m_combatEventMessage, model, positionX, positionY, positionZ))
		{
			LOG_WARN(Core, "[FXManager] CombatEvent position fallback used");
			positionX = model.playerStats.positionX;
			positionY = model.playerStats.positionY;
			positionZ = model.playerStats.positionZ;
		}

		return SpawnFxById(mappingIt->second, positionX, positionY, positionZ);
	}

	bool FXManager::ResolveCombatFxPosition(const engine::server::CombatEventMessage& message, const UIModel& model, float& outX, float& outY, float& outZ) const
	{
		if (message.targetEntityId == model.playerStats.playerEntityId && model.playerStats.hasSnapshot)
		{
			outX = model.playerStats.positionX;
			outY = model.playerStats.positionY;
			outZ = model.playerStats.positionZ;
			return true;
		}

		if (message.targetEntityId == model.targetStats.entityId && model.targetStats.hasTarget && model.targetStats.hasPosition)
		{
			outX = model.targetStats.positionX;
			outY = model.targetStats.positionY;
			outZ = model.targetStats.positionZ;
			return true;
		}

		return false;
	}

	template <typename TFx>
	void FXManager::RemoveExpired(std::vector<TFx>& entries)
	{
		entries.erase(
			std::remove_if(
				entries.begin(),
				entries.end(),
				[](const TFx& fx)
				{
					return fx.remainingSeconds <= 0.0f;
				}),
			entries.end());
	}
}
