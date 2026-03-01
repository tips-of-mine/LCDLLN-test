#pragma once

/**
 * @file AudioEngine.h
 * @brief Audio engine wrapper (OpenAL): 3D sounds, buses SFX/Music/UI, zone ambience. M17.4.
 */

#include "engine/audio/IAudioListenerUpdate.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::audio {

/** @brief Bus index for volume control (SFX, Music, UI). */
enum class Bus : uint8_t {
    SFX = 0,
    Music = 1,
    UI = 2,
};

/** @brief Opaque sound handle returned by LoadSound. */
using SoundId = uint32_t;

/** @brief Invalid sound id (load failed or not loaded). */
constexpr SoundId kInvalidSoundId = 0;

/**
 * @brief Audio engine: init/shutdown, 3D playback (position, min/max distance), listener, buses, zone ambience.
 * Content paths: pass content root (e.g. from Config paths.content) to LoadSound/LoadZoneAudio.
 */
class AudioEngine : public IAudioListenerUpdate {
public:
    AudioEngine() = default;
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /**
     * @brief Initialises the audio device and context. Call once at startup.
     * @return true if initialisation succeeded.
     */
    [[nodiscard]] bool Init();

    /** @brief Shuts down the audio device and frees all buffers/sources. */
    void Shutdown();

    /** @brief Returns true if Init() succeeded and Shutdown() has not been called. */
    [[nodiscard]] bool IsValid() const { return m_valid; }

    /**
     * @brief Loads a sound from a path relative to paths.content (e.g. "audio/sfx/impact.wav").
     * @return SoundId or kInvalidSoundId on failure. Avoid loading very large WAVs (streaming later).
     */
    [[nodiscard]] SoundId LoadSound(const std::string& contentRoot, const std::string& relativePath);

    /**
     * @brief Plays a 3D sound at the given position with distance rolloff.
     * @param position World position [x, y, z].
     * @param soundId   From LoadSound.
     * @param minDistance Distance at which gain is 1 (closer = no louder).
     * @param maxDistance Beyond this distance sound is silent (or minimal).
     * @param bus       Bus for volume (SFX/Music/UI).
     */
    void Play3D(const float position[3], SoundId soundId, float minDistance, float maxDistance, Bus bus = Bus::SFX);

    /**
     * @brief Sets the listener position, velocity and orientation for 3D spatialisation (IAudioListenerUpdate).
     */
    void SetListener(const float position[3], const float velocity[3], const float forward[3], const float up[3]) override;

    /**
     * @brief Sets the volume for a bus (0.f to 1.f). Applied to future and currently playing sounds on that bus.
     */
    void SetBusVolume(Bus bus, float volume);

    /**
     * @brief Loads zone audio definitions from a path relative to content (e.g. "audio/zone_audio.json").
     * JSON format: { "zones": [ { "id": "zone_01", "ambience": "audio/ambience_forest.wav", "loop": true } ] }
     */
    [[nodiscard]] bool LoadZoneAudio(const std::string& contentRoot, const std::string& relativePath);

    /**
     * @brief Starts playing the ambience loop for the given zone (stops previous zone ambience).
     * @param zoneId Zone id as defined in zone_audio.json. Empty string stops ambience.
     */
    void PlayZoneAmbience(const std::string& zoneId);

    /** @brief Call once per frame to update internal state (e.g. stop finished one-shots). Optional for MVP. */
    void Update();

private:
    struct ZoneDef {
        std::string ambiencePath;
        bool loop = true;
    };
    struct SourceState;

    void ReleaseSourcesAndBuffers();
    SoundId LoadWavIntoBuffer(const std::string& fullPath);

    bool m_valid = false;
    void* m_device = nullptr;
    void* m_context = nullptr;
    float m_busVolumes[3] = {1.f, 1.f, 1.f};
    std::unordered_map<std::string, ZoneDef> m_zoneDefs;
    std::unordered_map<std::string, SoundId> m_zoneAmbienceSounds;
    std::string m_currentZoneAmbience;
    std::string m_contentRoot;
    uint32_t m_zoneAmbienceSource = 0;
    std::vector<uint32_t> m_buffers;
    std::vector<SourceState> m_sources;
    SoundId m_nextSoundId = 1;
};

} // namespace engine::audio
