#pragma once

#include "engine/core/Config.h"
#include "engine/math/Math.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::audio { class MaMenuMusic; }

namespace engine::audio
{
	/// One named audio bus with a normalized volume multiplier.
	struct AudioBusDefinition
	{
		std::string id;
		float volume = 1.0f;
	};

	/// One sound definition loaded from zone audio data.
	struct AudioSoundDefinition
	{
		std::string id;
		std::string relativePath;
		std::string busId;
		float minDistanceMeters = 1.0f;
		float maxDistanceMeters = 25.0f;
		bool loop = false;
	};

	/// One zone ambience entry mapping a zone id to one looping 3D sound.
	struct ZoneAmbienceDefinition
	{
		uint32_t zoneId = 0;
		std::string soundId;
		engine::math::Vec3 position{};
	};

	/// One active runtime 3D sound instance tracked by the MVP audio wrapper.
	struct Active3DSound
	{
		uint32_t instanceId = 0;
		const AudioSoundDefinition* definition = nullptr;
		engine::math::Vec3 position{};
		float gain = 0.0f;
		uint32_t zoneId = 0;
		bool zoneLoop = false;
		bool active = false;
	};

	/// Minimal audio wrapper handling 3D attenuation, buses, and zone ambience data.
	class AudioEngine final
	{
	public:
		/// Construct an uninitialized audio engine wrapper.
		AudioEngine() = default;

		/// Shutdown audio state during destruction.
		~AudioEngine();

		/// Initialize the audio wrapper and load buses, sounds, and zone ambience metadata.
		bool Init(const engine::core::Config& config);

		/// Release all active sounds and loaded audio metadata.
		void Shutdown();

		/// Spawn one 3D sound instance at the given world position and return its instance id, or 0 on failure.
		uint32_t Play3D(const engine::math::Vec3& position, std::string_view soundId);

		/// Update the listener position and velocity used for 3D attenuation.
		bool SetListener(const engine::math::Vec3& position, const engine::math::Vec3& velocity);

		/// Set one named bus volume multiplier in the normalized range [0, 1].
		bool SetBusVolume(std::string_view busId, float volume);

		/// Set global master volume multiplier in the normalized range [0, 1].
		bool SetMasterVolume(float volume);

		/// Return the currently active zone id.
		uint32_t GetCurrentZoneId() const { return m_currentZoneId; }

		/// Activate the ambience loop configured for the requested zone id.
		bool SetZone(uint32_t zoneId);

		/// Advance runtime audio state and refresh per-instance attenuation.
		bool Tick(float deltaSeconds);

		/// Démarre une musique menu en boucle (fichier résolu sur disque). Arrête une éventuelle piste précédente.
		bool StartMenuMusic(const std::filesystem::path& filePathUtf8);
		void StopMenuMusic();

	private:
		/// Load zone audio data from the configured content-relative JSON file.
		bool LoadZoneAudio();

		/// Compute the attenuated gain of one sound instance against the current listener state.
		float ComputeGain(const Active3DSound& sound) const;

		const engine::core::Config* m_config = nullptr;
		std::unordered_map<std::string, AudioBusDefinition> m_buses;
		std::unordered_map<std::string, AudioSoundDefinition> m_sounds;
		std::vector<ZoneAmbienceDefinition> m_zoneAmbience;
		std::vector<Active3DSound> m_activeSounds;
		engine::math::Vec3 m_listenerPosition{};
		engine::math::Vec3 m_listenerVelocity{};
		std::string m_zoneAudioRelativePath;
		uint32_t m_currentZoneId = 0;
		uint32_t m_nextInstanceId = 1;
		float m_masterVolume = 1.0f;
		bool m_initialized = false;
		std::unique_ptr<MaMenuMusic> m_menuMusic;
	};
}
