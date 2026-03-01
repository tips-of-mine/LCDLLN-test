/**
 * @file AudioEngine.cpp
 * @brief OpenAL-backed audio engine: 3D sounds, buses, zone ambience. M17.4.
 */

#include "engine/audio/AudioEngine.h"

#include <AL/alc.h>
#include <cstdio>
#include <AL/al.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>

namespace engine::audio {

namespace {

constexpr size_t kMaxSources = 24u;

bool LoadWavFile(const std::string& path, std::vector<uint8_t>& outData, ALenum& outFormat, ALsizei& outSampleRate) {
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    char riff[4], wave[4];
    f.read(riff, 4);
    uint32_t fileSize = 0;
    f.read(reinterpret_cast<char*>(&fileSize), 4);
    f.read(wave, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0)
        return false;
    uint16_t numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    bool gotFmt = false, gotData = false;
    uint32_t dataSize = 0;
    while (f) {
        char chunkId[4];
        if (!f.read(chunkId, 4)) break;
        uint32_t chunkSize = 0;
        if (!f.read(reinterpret_cast<char*>(&chunkSize), 4)) break;
        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            uint16_t audioFormat = 0;
            f.read(reinterpret_cast<char*>(&audioFormat), 2);
            if (audioFormat != 1) return false; // PCM only
            f.read(reinterpret_cast<char*>(&numChannels), 2);
            f.read(reinterpret_cast<char*>(&sampleRate), 4);
            uint32_t byteRate = 0;
            f.read(reinterpret_cast<char*>(&byteRate), 4);
            uint16_t blockAlign = 0;
            f.read(reinterpret_cast<char*>(&blockAlign), 2);
            f.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            gotFmt = true;
            if (chunkSize > 16)
                f.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataSize = chunkSize;
            outData.resize(chunkSize);
            if (!f.read(reinterpret_cast<char*>(outData.data()), chunkSize))
                return false;
            gotData = true;
            break;
        } else {
            f.seekg(chunkSize, std::ios::cur);
        }
    }
    if (!gotFmt || !gotData || numChannels == 0 || bitsPerSample == 0)
        return false;
    if (numChannels == 1 && bitsPerSample == 8) outFormat = AL_FORMAT_MONO8;
    else if (numChannels == 1 && bitsPerSample == 16) outFormat = AL_FORMAT_MONO16;
    else if (numChannels == 2 && bitsPerSample == 8) outFormat = AL_FORMAT_STEREO8;
    else if (numChannels == 2 && bitsPerSample == 16) outFormat = AL_FORMAT_STEREO16;
    else return false;
    outSampleRate = static_cast<ALsizei>(sampleRate);
    return true;
}

} // namespace

struct AudioEngine::SourceState {
    ALuint id = 0;
    bool inUse = false;
};

AudioEngine::~AudioEngine() {
    Shutdown();
}

bool AudioEngine::Init() {
    if (m_valid)
        return true;
    ALCdevice* device = alcOpenDevice(nullptr);
    if (!device) {
        std::fprintf(stderr, "AudioEngine: alcOpenDevice failed\n");
        return false;
    }
    ALCcontext* context = alcCreateContext(device, nullptr);
    if (!context) {
        alcCloseDevice(device);
        std::fprintf(stderr, "AudioEngine: alcCreateContext failed\n");
        return false;
    }
    if (!alcMakeContextCurrent(context)) {
        alcDestroyContext(context);
        alcCloseDevice(device);
        std::fprintf(stderr, "AudioEngine: alcMakeContextCurrent failed\n");
        return false;
    }
    m_device = device;
    m_context = context;
    m_sources.resize(kMaxSources);
    for (size_t i = 0; i < kMaxSources; ++i) {
        ALuint sid = 0;
        alGenSources(1, &sid);
        m_sources[i].id = sid;
        m_sources[i].inUse = false;
    }
    m_zoneAmbienceSource = 0;
    ALuint ambSrc = 0;
    alGenSources(1, &ambSrc);
    m_zoneAmbienceSource = ambSrc;
    m_valid = true;
    return true;
}

void AudioEngine::Shutdown() {
    if (!m_valid) return;
    ReleaseSourcesAndBuffers();
    if (m_zoneAmbienceSource) {
        alDeleteSources(1, &m_zoneAmbienceSource);
        m_zoneAmbienceSource = 0;
    }
    for (auto& s : m_sources) {
        if (s.id) alDeleteSources(1, &s.id);
        s.id = 0;
        s.inUse = false;
    }
    m_sources.clear();
    ALCcontext* ctx = static_cast<ALCcontext*>(m_context);
    ALCdevice* dev = static_cast<ALCdevice*>(m_device);
    alcMakeContextCurrent(nullptr);
    if (ctx) alcDestroyContext(ctx);
    if (dev) alcCloseDevice(dev);
    m_context = nullptr;
    m_device = nullptr;
    m_valid = false;
    m_zoneDefs.clear();
    m_zoneAmbienceSounds.clear();
    m_currentZoneAmbience.clear();
    m_contentRoot.clear();
}

SoundId AudioEngine::LoadSound(const std::string& contentRoot, const std::string& relativePath) {
    if (!m_valid) return kInvalidSoundId;
    std::string fullPath = contentRoot;
    if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\')
        fullPath += '/';
    fullPath += relativePath;
    return LoadWavIntoBuffer(fullPath);
}

SoundId AudioEngine::LoadWavIntoBuffer(const std::string& fullPath) {
    std::vector<uint8_t> data;
    ALenum format = AL_FORMAT_MONO16;
    ALsizei sampleRate = 44100;
    if (!LoadWavFile(fullPath, data, format, sampleRate)) {
        std::fprintf(stderr, "AudioEngine: failed to load WAV '%s'\n", fullPath.c_str());
        return kInvalidSoundId;
    }
    ALuint buf = 0;
    alGenBuffers(1, &buf);
    if (!buf) return kInvalidSoundId;
    alBufferData(buf, format, data.data(), static_cast<ALsizei>(data.size()), sampleRate);
    m_buffers.push_back(buf);
    return static_cast<SoundId>(buf);
}

void AudioEngine::Play3D(const float position[3], SoundId soundId, float minDistance, float maxDistance, Bus bus) {
    if (!m_valid || soundId == kInvalidSoundId) return;
    ALuint bid = static_cast<ALuint>(soundId);
    SourceState* freeSlot = nullptr;
    for (auto& s : m_sources) {
        if (s.inUse) {
            ALint state = 0;
            alGetSourcei(s.id, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) {
                s.inUse = false;
                freeSlot = &s;
                break;
            }
        } else {
            freeSlot = &s;
            break;
        }
    }
    if (!freeSlot) return;
    ALuint sid = freeSlot->id;
    freeSlot->inUse = true;
    alSourcei(sid, AL_BUFFER, static_cast<ALint>(bid));
    alSource3f(sid, AL_POSITION, position[0], position[1], position[2]);
    alSourcef(sid, AL_REFERENCE_DISTANCE, minDistance);
    alSourcef(sid, AL_MAX_DISTANCE, maxDistance);
    alSourcef(sid, AL_GAIN, m_busVolumes[static_cast<size_t>(bus)]);
    alSourcei(sid, AL_LOOPING, AL_FALSE);
    alSourcePlay(sid);
}

void AudioEngine::SetListener(const float position[3], const float velocity[3], const float forward[3], const float up[3]) {
    if (!m_valid) return;
    alListener3f(AL_POSITION, position[0], position[1], position[2]);
    alListener3f(AL_VELOCITY, velocity[0], velocity[1], velocity[2]);
    float orient[6] = { forward[0], forward[1], forward[2], up[0], up[1], up[2] };
    alListenerfv(AL_ORIENTATION, orient);
}

void AudioEngine::SetBusVolume(Bus bus, float volume) {
    size_t i = static_cast<size_t>(bus);
    if (i < 3u)
        m_busVolumes[i] = std::clamp(volume, 0.f, 1.f);
}

bool AudioEngine::LoadZoneAudio(const std::string& contentRoot, const std::string& relativePath) {
    if (!m_valid) return false;
    m_contentRoot = contentRoot;
    std::string fullPath = contentRoot;
    if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\')
        fullPath += '/';
    fullPath += relativePath;
    std::ifstream f(fullPath);
    if (!f) {
        std::fprintf(stderr, "AudioEngine: zone audio file not found '%s'\n", fullPath.c_str());
        return false;
    }
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(f);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "AudioEngine: zone_audio.json parse error: %s\n", e.what());
        return false;
    }
    auto zones = j.find("zones");
    if (zones == j.end() || !zones->is_array())
        return true;
    for (const auto& zone : *zones) {
        if (!zone.is_object()) continue;
        auto id = zone.find("id");
        auto amb = zone.find("ambience");
        if (id == zone.end() || !id->is_string() || amb == zone.end() || !amb->is_string())
            continue;
        ZoneDef def;
        def.ambiencePath = amb->get<std::string>();
        def.loop = true;
        auto loopIt = zone.find("loop");
        if (loopIt != zone.end() && loopIt->is_boolean())
            def.loop = loopIt->get<bool>();
        m_zoneDefs[id->get<std::string>()] = std::move(def);
    }
    return true;
}

void AudioEngine::PlayZoneAmbience(const std::string& zoneId) {
    if (!m_valid) return;
    if (zoneId == m_currentZoneAmbience) return;
    if (m_zoneAmbienceSource) {
        alSourceStop(m_zoneAmbienceSource);
        alSourcei(m_zoneAmbienceSource, AL_BUFFER, 0);
    }
    m_currentZoneAmbience = zoneId;
    if (zoneId.empty()) return;
    auto it = m_zoneDefs.find(zoneId);
    if (it == m_zoneDefs.end()) return;
    const ZoneDef& def = it->second;
    auto soundIt = m_zoneAmbienceSounds.find(zoneId);
    SoundId sid = kInvalidSoundId;
    if (soundIt != m_zoneAmbienceSounds.end()) {
        sid = soundIt->second;
    } else {
        std::string fullPath = m_contentRoot;
        if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\')
            fullPath += '/';
        fullPath += def.ambiencePath;
        sid = LoadWavIntoBuffer(fullPath);
        if (sid != kInvalidSoundId)
            m_zoneAmbienceSounds[zoneId] = sid;
    }
    if (sid != kInvalidSoundId && m_zoneAmbienceSource) {
        alSourcei(m_zoneAmbienceSource, AL_BUFFER, static_cast<ALint>(sid));
        alSourcei(m_zoneAmbienceSource, AL_LOOPING, def.loop ? AL_TRUE : AL_FALSE);
        alSource3f(m_zoneAmbienceSource, AL_POSITION, 0.f, 0.f, 0.f);
        alSourcef(m_zoneAmbienceSource, AL_GAIN, m_busVolumes[static_cast<size_t>(Bus::Music)]);
        alSourcePlay(m_zoneAmbienceSource);
    }
}

void AudioEngine::Update() {
    if (!m_valid) return;
    for (auto& s : m_sources) {
        if (!s.inUse) continue;
        ALint state = 0;
        alGetSourcei(s.id, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING)
            s.inUse = false;
    }
}

void AudioEngine::ReleaseSourcesAndBuffers() {
    if (m_zoneAmbienceSource) {
        alSourceStop(m_zoneAmbienceSource);
        alSourcei(m_zoneAmbienceSource, AL_BUFFER, 0);
    }
    for (auto& s : m_sources) {
        if (s.id) {
            alSourceStop(s.id);
            alSourcei(s.id, AL_BUFFER, 0);
        }
        s.inUse = false;
    }
    for (ALuint buf : m_buffers)
        alDeleteBuffers(1, &buf);
    m_buffers.clear();
    m_zoneAmbienceSounds.clear();
}

} // namespace engine::audio
