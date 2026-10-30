#include "src/client/audio/AudioEngine.h"

#include "src/client/audio/MaMenuMusic.h"
#include "src/client/audio/SoundDevice.h"

#include "src/shared/core/Log.h"
#include "src/shared/platform/FileSystem.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

namespace engine::audio
{
	AudioEngine::AudioEngine() = default;
	AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;
	AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

	namespace
	{
		std::string Key(std::string_view base, size_t i, std::string_view field)
		{
			return std::string(base) + "[" + std::to_string(i) + "]." + std::string(field);
		}

		engine::math::Vec3 GetIndexedVec3(const engine::core::Config& c, std::string_view base, size_t i, std::string_view field)
		{
			const std::string p = Key(base, i, field);
			return engine::math::Vec3(static_cast<float>(c.GetDouble(p + "[0]", 0.0)),
				static_cast<float>(c.GetDouble(p + "[1]", 0.0)), static_cast<float>(c.GetDouble(p + "[2]", 0.0)));
		}
	}

	AudioEngine::~AudioEngine()
	{
		Shutdown();
	}

	size_t AudioEngine::PickVariant(size_t count, size_t last, float u)
	{
		if (count <= 1)
			return 0;
		u = std::clamp(u, 0.0f, 0.99999f);
		if (last >= count) // premier tirage : toutes les variantes
			return std::min(static_cast<size_t>(u * static_cast<float>(count)), count - 1);
		// Tirage parmi les count-1 autres variantes : jamais deux fois la même d'affilée.
		size_t k = static_cast<size_t>(u * static_cast<float>(count - 1));
		if (k >= last)
			++k;
		return std::min(k, count - 1);
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

		// P3.2 -- Sons de pas par couche splat (zones historiques de l'éditeur).
		const std::string footstepManifestRel = config.GetString("audio.footstep_manifest_path", "");
		if (!footstepManifestRel.empty())
		{
			const std::filesystem::path manifestAbs =
				engine::platform::FileSystem::ResolveContentPath(config, footstepManifestRel);
			if (!RegisterFootstepSoundsFromManifest(manifestAbs))
				LOG_WARN(Core, "[AudioEngine] Footstep manifest registration failed (path='{}')", footstepManifestRel);
		}

		// SND.2 — périphérique de lecture réelle (désactivable : audio.sfx_device_enabled).
		if (config.GetBool("audio.sfx_device_enabled", true))
		{
			auto device = std::make_unique<SoundDevice>();
			SoundDevice::Options opt;
			opt.maxVoices = static_cast<uint32_t>(std::clamp<int64_t>(config.GetInt("audio.max_voices", 64), 8, 256));
			if (device->Init(opt))
			{
				m_device = std::move(device);
				for (const auto& [id, bus] : m_buses)
					m_device->SetBusVolume(id, bus.volume);
				m_device->SetMasterVolume(m_masterVolume);
			}
		}

		m_initialized = true;
		LOG_INFO(Core, "[AudioEngine] Init OK (lecture={}, buses={}, sounds={}, zones={})",
			m_device ? "miniaudio" : "muette", m_buses.size(), m_sounds.size(), m_zoneAmbience.size());
		return true;
	}

	std::string AudioEngine::FootstepSoundIdForLayer(uint32_t layer)
	{
		return std::string("_footstep_layer_") + std::to_string(layer);
	}

	bool AudioEngine::RegisterFootstepSoundsFromManifest(const std::filesystem::path& manifestAbsolutePath)
	{
		if (m_config == nullptr)
		{
			LOG_ERROR(Core, "[AudioEngine] RegisterFootstepSoundsFromManifest FAILED: missing config (call Init first)");
			return false;
		}
		engine::core::Config manifest;
		if (!manifest.LoadFromFile(manifestAbsolutePath.string()))
		{
			LOG_ERROR(Core, "[AudioEngine] RegisterFootstepSoundsFromManifest FAILED: unable to read '{}'",
				manifestAbsolutePath.string());
			return false;
		}
		const int64_t manifestVersion = manifest.GetInt("lcdlln_runtime_manifest_version", 0);
		if (manifestVersion < 4)
		{
			LOG_WARN(Core,
				"[AudioEngine] Footstep manifest version {} < 4 (clef splat_layer_footstep_audio_refs absente) ; aucun son de pas enregistre",
				manifestVersion);
			return false;
		}

		// Bus par defaut : "SFX" si present, sinon le premier bus disponible (l'audio sortira
		// quand meme, juste sans le mixage SFX dedie).
		std::string defaultBusId = "SFX";
		if (m_buses.find(defaultBusId) == m_buses.end() && !m_buses.empty())
		{
			defaultBusId = m_buses.begin()->first;
		}

		uint32_t registeredCount = 0;
		for (uint32_t layer = 0; layer < 4u; ++layer)
		{
			const std::string key = "splat_layer_footstep_audio_refs[" + std::to_string(layer) + "]";
			const std::string relPath = manifest.GetString(key, "");
			if (relPath.empty())
			{
				continue;
			}
			AudioSoundDefinition sound{};
			sound.id = FootstepSoundIdForLayer(layer);
			sound.relativePath = relPath;
			sound.busId = defaultBusId;
			sound.minDistanceMeters = 0.5f;
			sound.maxDistanceMeters = 12.0f;
			sound.loop = false;

			const std::filesystem::path soundPath =
				engine::platform::FileSystem::ResolveContentPath(*m_config, relPath);
			if (!engine::platform::FileSystem::Exists(soundPath))
			{
				LOG_WARN(Core,
					"[AudioEngine] Footstep audio missing on disk (layer={}, path='{}') ; metadata enregistree, lecture echouera silencieusement",
					layer, relPath);
			}

			m_sounds[sound.id] = sound;
			++registeredCount;
			LOG_INFO(Core, "[AudioEngine] Registered footstep sound (layer={}, id='{}', path='{}')",
				layer, sound.id, sound.relativePath);
		}

		if (registeredCount == 0u)
		{
			LOG_INFO(Core,
				"[AudioEngine] Footstep manifest charge mais aucune couche n'avait de son assigne (champ vide) ; aucun son enregistre");
			return false;
		}
		LOG_INFO(Core, "[AudioEngine] Footstep registration OK ({} couches sur 4)", registeredCount);
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
			m_menuMusic = std::make_unique<MaMenuMusic>();
		return m_menuMusic->PlayLoop(filePathUtf8);
	}

	void AudioEngine::StopMenuMusic()
	{
		if (m_menuMusic)
			m_menuMusic->Stop();
	}

	void AudioEngine::Shutdown()
	{
		LOG_DEBUG(Core, "[AUDIO] Shutdown enter");
		StopMenuMusic();
		m_menuMusic.reset();
		m_zoneLoopVoices.clear();
		if (m_device)
		{
			m_device->Shutdown();
			m_device.reset();
		}
		m_zoneAmbience.clear();
		m_sounds.clear();
		m_lastVariant.clear();
		m_buses.clear();
		m_listenerPosition = {};
		m_listenerVelocity = {};
		m_zoneAudioRelativePath.clear();
		m_currentZoneId = 0;
		m_masterVolume = 1.0f;
		m_config = nullptr;
		m_initialized = false;
	}

	bool AudioEngine::HasSound(std::string_view soundId) const
	{
		return m_sounds.find(std::string(soundId)) != m_sounds.end();
	}

	bool AudioEngine::IsDeviceReady() const
	{
		return m_device && m_device->IsReady();
	}

	float AudioEngine::BusVolume(const std::string& id) const
	{
		const auto it = m_buses.find(id);
		return it != m_buses.end() ? it->second.volume : 1.0f;
	}

	uint32_t AudioEngine::PlayInternal(const AudioSoundDefinition& def, float volumeScale, bool spatial,
		const engine::math::Vec3& position, bool loop, uint32_t fadeMs)
	{
		if (!IsDeviceReady() || m_config == nullptr || def.FileCount() == 0)
			return 0;
		std::uniform_real_distribution<float> uni(0.0f, 1.0f);
		const size_t count = def.FileCount();
		auto lastIt = m_lastVariant.find(def.id);
		const size_t last = lastIt != m_lastVariant.end() ? lastIt->second : count;
		const size_t pick = PickVariant(count, last, uni(m_rng));
		m_lastVariant[def.id] = pick;

		SoundPlayRequest rq;
		rq.path = engine::platform::FileSystem::ResolveContentPath(*m_config, def.File(pick)).string();
		rq.bus = def.busId;
		rq.volume = std::max(0.0f, def.volume * volumeScale * (1.0f + def.volumeJitter * (2.0f * uni(m_rng) - 1.0f)));
		rq.pitch = 1.0f + def.pitchJitter * (2.0f * uni(m_rng) - 1.0f);
		rq.spatial = spatial;
		rq.position = position;
		rq.minDistance = def.minDistanceMeters;
		rq.maxDistance = def.maxDistanceMeters;
		rq.loop = loop;
		rq.stream = def.stream || loop;
		rq.fadeInMs = fadeMs;
		return m_device->Play(rq);
	}

	uint32_t AudioEngine::Play3D(const engine::math::Vec3& position, std::string_view soundId, float volumeScale)
	{
		if (!m_initialized)
			return 0;
		const auto it = m_sounds.find(std::string(soundId));
		if (it == m_sounds.end())
		{
			LOG_DEBUG(Core, "[AudioEngine] Play3D : son inconnu '{}'", soundId);
			return 0;
		}
		return PlayInternal(it->second, volumeScale, true, position, false, 0);
	}

	uint32_t AudioEngine::Play2D(std::string_view soundId, float volumeScale)
	{
		if (!m_initialized)
			return 0;
		const auto it = m_sounds.find(std::string(soundId));
		if (it == m_sounds.end())
		{
			LOG_DEBUG(Core, "[AudioEngine] Play2D : son inconnu '{}'", soundId);
			return 0;
		}
		return PlayInternal(it->second, volumeScale, false, {}, false, 0);
	}

	uint32_t AudioEngine::PlayLoop(std::string_view soundId, float volume, uint32_t fadeMs)
	{
		if (!m_initialized)
			return 0;
		const auto it = m_sounds.find(std::string(soundId));
		if (it == m_sounds.end())
		{
			LOG_DEBUG(Core, "[AudioEngine] PlayLoop : son inconnu '{}'", soundId);
			return 0;
		}
		return PlayInternal(it->second, volume, false, {}, true, fadeMs);
	}

	void AudioEngine::SetSoundVolume(uint32_t voice, float volume, uint32_t fadeMs)
	{
		if (m_device && voice != 0)
			m_device->SetVoiceVolume(voice, volume, fadeMs);
	}

	void AudioEngine::StopSound(uint32_t voice, uint32_t fadeMs)
	{
		if (m_device && voice != 0)
			m_device->Stop(voice, fadeMs);
	}

	bool AudioEngine::SetListener(const engine::math::Vec3& position, const engine::math::Vec3& velocity)
	{
		if (!m_initialized)
			return false;
		m_listenerPosition = position;
		m_listenerVelocity = velocity;
		return true;
	}

	void AudioEngine::SetListenerOrientation(const engine::math::Vec3& forward, const engine::math::Vec3& up)
	{
		if (m_device)
			m_device->SetListener(m_listenerPosition, forward, up);
	}

	bool AudioEngine::SetBusVolume(std::string_view busId, float volume)
	{
		if (!m_initialized)
			return false;
		const float clamped = std::clamp(volume, 0.0f, 1.0f);
		auto it = m_buses.find(std::string(busId));
		if (it == m_buses.end())
		{
			// Bus non déclaré dans zone_audio.json : créé à la volée (ex. Ambience).
			m_buses.emplace(std::string(busId), AudioBusDefinition{ std::string(busId), clamped });
			if (m_device)
				m_device->SetBusVolume(busId, clamped);
			return true;
		}
		// Appelé chaque frame pour la météo : on ignore les variations inaudibles.
		constexpr float kVolumeEpsilon = 1.0f / 256.0f;
		if (std::abs(clamped - it->second.volume) < kVolumeEpsilon)
			return true;
		it->second.volume = clamped;
		if (m_device)
			m_device->SetBusVolume(busId, clamped);
		LOG_DEBUG(Core, "[AudioEngine] Bus volume updated (bus={}, volume={:.2f})", busId, clamped);
		return true;
	}

	bool AudioEngine::SetMasterVolume(float volume)
	{
		if (!m_initialized)
			return false;
		m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
		if (m_device)
			m_device->SetMasterVolume(m_masterVolume);
		LOG_INFO(Core, "[AudioEngine] Master volume updated ({:.2f})", m_masterVolume);
		return true;
	}

	bool AudioEngine::SetZone(uint32_t zoneId)
	{
		if (!m_initialized)
			return false;
		m_currentZoneId = zoneId;
		for (uint32_t v : m_zoneLoopVoices)
			StopSound(v, 1500);
		m_zoneLoopVoices.clear();

		bool activated = false;
		for (const ZoneAmbienceDefinition& ambience : m_zoneAmbience)
		{
			if (ambience.zoneId != zoneId)
				continue;
			const auto it = m_sounds.find(ambience.soundId);
			if (it == m_sounds.end())
			{
				LOG_WARN(Core, "[AudioEngine] Zone ambience skipped: unknown sound '{}' for zone {}", ambience.soundId, zoneId);
				continue;
			}
			// La musique du menu (zone 9999) est jouée par MaMenuMusic : pas de doublon.
			if (it->second.busId == "Music")
				continue;
			if (const uint32_t v = PlayInternal(it->second, 1.0f, false, {}, true, 1500))
			{
				m_zoneLoopVoices.push_back(v);
				activated = true;
			}
		}
		LOG_INFO(Core, "[AudioEngine] Zone {} : {} boucle(s) d'ambiance", zoneId, m_zoneLoopVoices.size());
		return activated;
	}

	bool AudioEngine::Tick(float deltaSeconds)
	{
		if (!m_initialized || deltaSeconds <= 0.0f)
			return false;
		if (m_device)
			m_device->Update();
		if (m_menuMusic && m_menuMusic->IsActive())
			m_menuMusic->SetLinearGain(m_masterVolume * BusVolume("Music"));
		return true;
	}

	bool AudioEngine::LoadZoneAudio()
	{
		m_buses.clear();
		m_sounds.clear();
		m_zoneAmbience.clear();
		if (m_config == nullptr)
			return false;

		const std::filesystem::path fullPath = engine::platform::FileSystem::ResolveContentPath(*m_config, m_zoneAudioRelativePath);
		engine::core::Config c;
		if (!c.LoadFromFile(fullPath.string()))
		{
			LOG_ERROR(Core, "[AudioEngine] LoadZoneAudio FAILED: invalid json '{}'", fullPath.string());
			return false;
		}

		for (size_t i = 0; c.Has(Key("buses", i, "id")); ++i)
		{
			AudioBusDefinition bus{};
			bus.id = c.GetString(Key("buses", i, "id"), "");
			bus.volume = static_cast<float>(c.GetDouble(Key("buses", i, "volume"), 1.0));
			if (!bus.id.empty())
				m_buses[bus.id] = bus;
		}

		size_t missing = 0;
		for (size_t i = 0; c.Has(Key("sounds", i, "id")); ++i)
		{
			AudioSoundDefinition s{};
			s.id = c.GetString(Key("sounds", i, "id"), "");
			s.relativePath = c.GetString(Key("sounds", i, "path"), "");
			s.busId = c.GetString(Key("sounds", i, "bus"), "SFX");
			s.minDistanceMeters = static_cast<float>(c.GetDouble(Key("sounds", i, "minDistance"), 1.0));
			s.maxDistanceMeters = static_cast<float>(c.GetDouble(Key("sounds", i, "maxDistance"), 25.0));
			s.loop = c.GetBool(Key("sounds", i, "loop"), false);
			s.volume = static_cast<float>(c.GetDouble(Key("sounds", i, "volume"), 1.0));
			s.pitchJitter = static_cast<float>(c.GetDouble(Key("sounds", i, "pitchJitter"), 0.0));
			s.volumeJitter = static_cast<float>(c.GetDouble(Key("sounds", i, "volumeJitter"), 0.0));
			s.stream = c.GetBool(Key("sounds", i, "stream"), false);
			for (size_t k = 0; k < 64; ++k)
			{
				const std::string vk = Key("sounds", i, "variants") + "[" + std::to_string(k) + "]";
				if (!c.Has(vk))
					break;
				const std::string v = c.GetString(vk, "");
				if (!v.empty())
					s.variants.push_back(v);
			}
			if (s.id.empty() || s.FileCount() == 0)
			{
				LOG_WARN(Core, "[AudioEngine] Skipped invalid sound definition at index {}", i);
				continue;
			}
			for (size_t f = 0; f < s.FileCount(); ++f)
				if (!engine::platform::FileSystem::Exists(engine::platform::FileSystem::ResolveContentPath(*m_config, s.File(f))))
				{
					++missing;
					LOG_WARN(Core, "[AudioEngine] Fichier son absent : {} ({})", s.File(f), s.id);
				}
			m_sounds[s.id] = std::move(s);
		}

		for (size_t i = 0; c.Has(Key("zones", i, "zoneId")); ++i)
		{
			ZoneAmbienceDefinition z{};
			z.zoneId = static_cast<uint32_t>(c.GetInt(Key("zones", i, "zoneId"), 0));
			z.soundId = c.GetString(Key("zones", i, "ambienceSoundId"), "");
			z.position = GetIndexedVec3(c, "zones", i, "position");
			if (!z.soundId.empty())
				m_zoneAmbience.push_back(z);
		}
		LOG_INFO(Core, "[AudioEngine] Catalogue : {} bus, {} sons, {} ambiance(s) de zone, {} fichier(s) absent(s)",
			m_buses.size(), m_sounds.size(), m_zoneAmbience.size(), missing);
		return !m_sounds.empty();
	}
}
