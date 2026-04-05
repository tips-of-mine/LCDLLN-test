#include "engine/audio/AudioEngine.h"

#include "engine/audio/MaMenuMusic.h"

#include "engine/core/Log.h"
#include "engine/platform/FileSystem.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>

namespace engine::audio
{
	namespace
	{
		bool HasIndexedKey(const engine::core::Config& config, std::string_view baseKey, size_t index, std::string_view field)
		{
			const std::string key = std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return config.Has(key);
		}

		std::string GetIndexedString(const engine::core::Config& config, std::string_view baseKey, size_t index, std::string_view field)
		{
			const std::string key = std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return config.GetString(key);
		}

		double GetIndexedDouble(const engine::core::Config& config, std::string_view baseKey, size_t index, std::string_view field, double fallback)
		{
			const std::string key = std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return config.GetDouble(key, fallback);
		}

		engine::math::Vec3 GetIndexedVec3(const engine::core::Config& config, std::string_view baseKey, size_t index, std::string_view field)
		{
			const std::string prefix = std::string(baseKey) + "[" + std::to_string(index) + "]." + std::string(field);
			return engine::math::Vec3(
				static_cast<float>(config.GetDouble(prefix + "[0]", 0.0)),
				static_cast<float>(config.GetDouble(prefix + "[1]", 0.0)),
				static_cast<float>(config.GetDouble(prefix + "[2]", 0.0)));
		}
	}

	AudioEngine::~AudioEngine()
	{
		Shutdown();
	}

	bool AudioEngine::Init(const engine::core::Config& config)
	{
		LOG_INFO(Core, "[AUDIO] Init enter");
		Shutdown();
		m_config = &config;
		m_zoneAudioRelativePath = config.GetString("audio.zone_audio_path", "audio/zone_audio.json");
		m_masterVolume = static_cast<float>(config.GetDouble("audio.master_volume", 1.0));
		if (!LoadZoneAudio())
		{
			LOG_ERROR(Core, "[AudioEngine] Init FAILED: unable to load '{}'", m_zoneAudioRelativePath);
			return false;
		}

		m_initialized = true;
		LOG_INFO(Core, "[AudioEngine] Init OK (3D=logical, menu_music=miniaudio if used, buses={}, sounds={}, zones={})",
			static_cast<uint32_t>(m_buses.size()),
			static_cast<uint32_t>(m_sounds.size()),
			static_cast<uint32_t>(m_zoneAmbience.size()));
		return true;
	}

	bool AudioEngine::StartMenuMusic(const std::filesystem::path& filePathUtf8)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[AudioEngine] StartMenuMusic ignored: not initialized");
			return false;
		}
		if (!m_menuMusic)
		{
			m_menuMusic = std::make_unique<MaMenuMusic>();
		}
		return m_menuMusic->PlayLoop(filePathUtf8);
	}

	void AudioEngine::StopMenuMusic()
	{
		if (m_menuMusic)
		{
			m_menuMusic->Stop();
		}
	}

	void AudioEngine::Shutdown()
	{
		LOG_DEBUG(Core, "[AUDIO] Shutdown enter");
		StopMenuMusic();
		m_menuMusic.reset();
		m_activeSounds.clear();
		m_zoneAmbience.clear();
		m_sounds.clear();
		m_buses.clear();
		m_listenerPosition = {};
		m_listenerVelocity = {};
		m_zoneAudioRelativePath.clear();
		m_currentZoneId = 0;
		m_nextInstanceId = 1;
		m_masterVolume = 1.0f;
		m_config = nullptr;
		m_initialized = false;
		LOG_INFO(Core, "[AudioEngine] Shutdown complete");
	}

	uint32_t AudioEngine::Play3D(const engine::math::Vec3& position, std::string_view soundId)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[AudioEngine] Play3D ignored: engine not initialized");
			return 0;
		}

		const auto soundIt = m_sounds.find(std::string(soundId));
		if (soundIt == m_sounds.end())
		{
			LOG_WARN(Core, "[AudioEngine] Play3D failed: unknown sound '{}'", soundId);
			return 0;
		}

		Active3DSound instance{};
		instance.instanceId = m_nextInstanceId++;
		instance.definition = &soundIt->second;
		instance.position = position;
		instance.gain = 0.0f;
		instance.active = true;
		m_activeSounds.push_back(instance);

		LOG_INFO(Core, "[AudioEngine] Play3D OK (sound_id={}, instance_id={}, bus={})",
			soundId, instance.instanceId, soundIt->second.busId);
		return instance.instanceId;
	}

	bool AudioEngine::SetListener(const engine::math::Vec3& position, const engine::math::Vec3& velocity)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[AudioEngine] SetListener ignored: engine not initialized");
			return false;
		}

		m_listenerPosition = position;
		m_listenerVelocity = velocity;
		return true;
	}

	bool AudioEngine::SetBusVolume(std::string_view busId, float volume)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[AudioEngine] SetBusVolume ignored: engine not initialized");
			return false;
		}

		auto it = m_buses.find(std::string(busId));
		if (it == m_buses.end())
		{
			LOG_WARN(Core, "[AudioEngine] SetBusVolume failed: unknown bus '{}'", busId);
			return false;
		}

		it->second.volume = std::clamp(volume, 0.0f, 1.0f);
		LOG_INFO(Core, "[AudioEngine] Bus volume updated (bus={}, volume={:.2f})", busId, it->second.volume);
		return true;
	}

	bool AudioEngine::SetMasterVolume(float volume)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[AudioEngine] SetMasterVolume ignored: engine not initialized");
			return false;
		}

		m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
		LOG_INFO(Core, "[AudioEngine] Master volume updated ({:.2f})", m_masterVolume);
		return true;
	}

	bool AudioEngine::SetZone(uint32_t zoneId)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[AudioEngine] SetZone ignored: engine not initialized");
			return false;
		}

		m_currentZoneId = zoneId;
		for (Active3DSound& sound : m_activeSounds)
		{
			if (sound.zoneLoop)
			{
				sound.active = false;
			}
		}

		bool activated = false;
		for (const ZoneAmbienceDefinition& ambience : m_zoneAmbience)
		{
			if (ambience.zoneId != zoneId)
			{
				continue;
			}

			const auto soundIt = m_sounds.find(ambience.soundId);
			if (soundIt == m_sounds.end())
			{
				LOG_WARN(Core, "[AudioEngine] Zone ambience skipped: unknown sound '{}' for zone {}", ambience.soundId, zoneId);
				continue;
			}

			Active3DSound instance{};
			instance.instanceId = m_nextInstanceId++;
			instance.definition = &soundIt->second;
			instance.position = ambience.position;
			instance.gain = 0.0f;
			instance.zoneId = zoneId;
			instance.zoneLoop = true;
			instance.active = true;
			m_activeSounds.push_back(instance);
			activated = true;

			LOG_INFO(Core, "[AudioEngine] Zone ambience activated (zone_id={}, sound_id={}, instance_id={})",
				zoneId, ambience.soundId, instance.instanceId);
		}

		if (!activated)
		{
			LOG_WARN(Core, "[AudioEngine] No ambience configured for zone {}", zoneId);
		}
		return activated;
	}

	bool AudioEngine::Tick(float deltaSeconds)
	{
		if (!m_initialized)
		{
			LOG_WARN(Core, "[AudioEngine] Tick ignored: engine not initialized");
			return false;
		}

		if (deltaSeconds <= 0.0f)
		{
			LOG_WARN(Core, "[AudioEngine] Tick ignored: invalid dt={:.4f}", deltaSeconds);
			return false;
		}

		uint32_t activeCount = 0;
		for (Active3DSound& sound : m_activeSounds)
		{
			if (!sound.active || sound.definition == nullptr)
			{
				continue;
			}

			sound.gain = ComputeGain(sound);
			++activeCount;
		}

		if (m_menuMusic && m_menuMusic->IsActive())
		{
			float musicBus = 1.0f;
			const auto busIt = m_buses.find("Music");
			if (busIt != m_buses.end())
			{
				musicBus = busIt->second.volume;
			}
			m_menuMusic->SetLinearGain(m_masterVolume * musicBus);
		}

		LOG_DEBUG(Core, "[AudioEngine] Tick OK (zone_id={}, active_sounds={}, listener=({:.2f}, {:.2f}, {:.2f}))",
			m_currentZoneId,
			activeCount,
			m_listenerPosition.x,
			m_listenerPosition.y,
			m_listenerPosition.z);
		return true;
	}

	bool AudioEngine::LoadZoneAudio()
	{
		m_buses.clear();
		m_sounds.clear();
		m_zoneAmbience.clear();

		if (m_config == nullptr)
		{
			LOG_ERROR(Core, "[AudioEngine] LoadZoneAudio FAILED: missing config");
			return false;
		}

		const std::filesystem::path fullPath = engine::platform::FileSystem::ResolveContentPath(*m_config, m_zoneAudioRelativePath);
		engine::core::Config audioConfig;
		if (!audioConfig.LoadFromFile(fullPath.string()))
		{
			LOG_ERROR(Core, "[AudioEngine] LoadZoneAudio FAILED: invalid json '{}'", fullPath.string());
			return false;
		}

		for (size_t i = 0; HasIndexedKey(audioConfig, "buses", i, "id"); ++i)
		{
			AudioBusDefinition bus{};
			bus.id = GetIndexedString(audioConfig, "buses", i, "id");
			bus.volume = static_cast<float>(GetIndexedDouble(audioConfig, "buses", i, "volume", 1.0));
			if (bus.id.empty())
			{
				LOG_WARN(Core, "[AudioEngine] Skipped invalid bus definition at index {}", static_cast<uint32_t>(i));
				continue;
			}

			m_buses[bus.id] = bus;
			LOG_INFO(Core, "[AudioEngine] Loaded bus '{}' (volume={:.2f})", bus.id, bus.volume);
		}

		for (size_t i = 0; HasIndexedKey(audioConfig, "sounds", i, "id"); ++i)
		{
			AudioSoundDefinition sound{};
			sound.id = GetIndexedString(audioConfig, "sounds", i, "id");
			sound.relativePath = GetIndexedString(audioConfig, "sounds", i, "path");
			sound.busId = GetIndexedString(audioConfig, "sounds", i, "bus");
			sound.minDistanceMeters = static_cast<float>(GetIndexedDouble(audioConfig, "sounds", i, "minDistance", 1.0));
			sound.maxDistanceMeters = static_cast<float>(GetIndexedDouble(audioConfig, "sounds", i, "maxDistance", 25.0));
			sound.loop = audioConfig.GetBool("sounds[" + std::to_string(i) + "].loop", false);

			if (sound.id.empty() || sound.relativePath.empty() || sound.busId.empty())
			{
				LOG_WARN(Core, "[AudioEngine] Skipped invalid sound definition at index {}", static_cast<uint32_t>(i));
				continue;
			}

			const std::filesystem::path soundPath = engine::platform::FileSystem::ResolveContentPath(*m_config, sound.relativePath);
			if (!engine::platform::FileSystem::Exists(soundPath))
			{
				LOG_WARN(Core, "[AudioEngine] Sound asset missing on disk, metadata kept (sound_id={}, path={})", sound.id, sound.relativePath);
			}
			else
			{
				LOG_INFO(Core, "[AudioEngine] Sound asset resolved (sound_id={}, path={})", sound.id, sound.relativePath);
			}

			m_sounds[sound.id] = sound;
			LOG_INFO(Core, "[AudioEngine] Loaded sound '{}' (bus={}, min={}, max={}, loop={})",
				sound.id, sound.busId, sound.minDistanceMeters, sound.maxDistanceMeters, sound.loop);
		}

		for (size_t i = 0; HasIndexedKey(audioConfig, "zones", i, "zoneId"); ++i)
		{
			ZoneAmbienceDefinition zone{};
			zone.zoneId = static_cast<uint32_t>(audioConfig.GetInt("zones[" + std::to_string(i) + "].zoneId", 0));
			zone.soundId = GetIndexedString(audioConfig, "zones", i, "ambienceSoundId");
			zone.position = GetIndexedVec3(audioConfig, "zones", i, "position");
			if (zone.soundId.empty())
			{
				LOG_WARN(Core, "[AudioEngine] Skipped invalid zone ambience at index {}", static_cast<uint32_t>(i));
				continue;
			}

			m_zoneAmbience.push_back(zone);
			LOG_INFO(Core, "[AudioEngine] Loaded zone ambience (zone_id={}, sound_id={})", zone.zoneId, zone.soundId);
		}

		if (m_buses.empty() || m_sounds.empty())
		{
			LOG_ERROR(Core, "[AudioEngine] LoadZoneAudio FAILED: missing buses or sounds in '{}'", m_zoneAudioRelativePath);
			return false;
		}

		return true;
	}

	float AudioEngine::ComputeGain(const Active3DSound& sound) const
	{
		if (sound.definition == nullptr)
		{
			return 0.0f;
		}

		const engine::math::Vec3 delta = m_listenerPosition - sound.position;
		const float distance = delta.Length();
		const float minDistance = std::max(0.01f, sound.definition->minDistanceMeters);
		const float maxDistance = std::max(minDistance, sound.definition->maxDistanceMeters);

		float attenuation = 1.0f;
		if (distance > minDistance)
		{
			const float range = std::max(0.01f, maxDistance - minDistance);
			attenuation = 1.0f - std::clamp((distance - minDistance) / range, 0.0f, 1.0f);
		}

		float busVolume = 1.0f;
		const auto busIt = m_buses.find(sound.definition->busId);
		if (busIt != m_buses.end())
		{
			busVolume = busIt->second.volume;
		}

		return attenuation * busVolume * m_masterVolume;
	}
}
