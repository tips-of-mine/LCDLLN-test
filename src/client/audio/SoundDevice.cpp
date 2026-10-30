// SND.2 — Lecture réelle des sons (cf. SoundDevice.h).
// L'implémentation de miniaudio (et le décodeur Ogg Vorbis) est compilée dans
// MaMenuMusic.cpp ; ici on n'inclut que l'en-tête.

#include "src/client/audio/SoundDevice.h"

#include "src/shared/core/Log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include "miniaudio.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <chrono>
#include <unordered_map>

namespace engine::audio
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
	}

	struct SoundDevice::Impl
	{
		struct Voice
		{
			ma_sound sound{};
			bool loop = false;
			bool stopping = false;
			Clock::time_point stopAt{};
		};

		ma_engine engine{};
		bool ready = false;
		Options options{};
		std::unordered_map<std::string, std::unique_ptr<ma_sound_group>> buses;
		std::unordered_map<uint32_t, std::unique_ptr<Voice>> voices;
		uint32_t nextVoice = 1;

		ma_sound_group* Bus(std::string_view id)
		{
			const std::string key(id.empty() ? std::string_view("SFX") : id);
			auto it = buses.find(key);
			if (it != buses.end())
				return it->second.get();
			auto group = std::make_unique<ma_sound_group>();
			if (ma_sound_group_init(&engine, 0, nullptr, group.get()) != MA_SUCCESS)
			{
				LOG_WARN(Core, "[SoundDevice] bus '{}' : creation impossible", key);
				return nullptr;
			}
			ma_sound_group* raw = group.get();
			buses.emplace(key, std::move(group));
			return raw;
		}

		void FreeVoice(Voice& v)
		{
			ma_sound_stop(&v.sound);
			ma_sound_uninit(&v.sound);
		}
	};

	SoundDevice::SoundDevice() : m_impl(std::make_unique<Impl>()) {}

	SoundDevice::~SoundDevice()
	{
		Shutdown();
	}

	bool SoundDevice::Init(const Options& options)
	{
		Shutdown();
		ma_engine_config cfg = ma_engine_config_init();
		cfg.noDevice = options.noDevice ? MA_TRUE : MA_FALSE;
		cfg.channels = options.channels;
		cfg.sampleRate = options.sampleRate;
		const ma_result r = ma_engine_init(&cfg, &m_impl->engine);
		if (r != MA_SUCCESS)
		{
			LOG_WARN(Core, "[SoundDevice] ma_engine_init impossible ({}) : jeu sans effets sonores", static_cast<int>(r));
			return false;
		}
		m_impl->options = options;
		m_impl->ready = true;
		LOG_INFO(Core, "[SoundDevice] pret ({} Hz, {} voix max{})", ma_engine_get_sample_rate(&m_impl->engine),
			options.maxVoices, options.noDevice ? ", sans peripherique" : "");
		return true;
	}

	void SoundDevice::Shutdown()
	{
		if (!m_impl || !m_impl->ready)
			return;
		for (auto& [id, v] : m_impl->voices)
			m_impl->FreeVoice(*v);
		m_impl->voices.clear();
		for (auto& [id, g] : m_impl->buses)
			ma_sound_group_uninit(g.get());
		m_impl->buses.clear();
		ma_engine_uninit(&m_impl->engine);
		m_impl->ready = false;
	}

	bool SoundDevice::IsReady() const
	{
		return m_impl && m_impl->ready;
	}

	void SoundDevice::SetBusVolume(std::string_view bus, float volume)
	{
		if (!IsReady())
			return;
		if (ma_sound_group* g = m_impl->Bus(bus))
			ma_sound_group_set_volume(g, std::clamp(volume, 0.0f, 1.0f));
	}

	void SoundDevice::SetMasterVolume(float volume)
	{
		if (IsReady())
			ma_engine_set_volume(&m_impl->engine, std::clamp(volume, 0.0f, 1.0f));
	}

	uint32_t SoundDevice::Play(const SoundPlayRequest& rq)
	{
		if (!IsReady() || rq.path.empty())
			return 0;
		if (m_impl->voices.size() >= m_impl->options.maxVoices && !rq.loop)
			return 0; // limite atteinte : les boucles passent toujours, un son ponctuel de plus est ignoré
		ma_sound_group* bus = m_impl->Bus(rq.bus);
		auto voice = std::make_unique<Impl::Voice>();
		ma_uint32 flags = rq.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
		if (!rq.spatial)
			flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
		const ma_result r = ma_sound_init_from_file(&m_impl->engine, rq.path.c_str(), flags, bus, nullptr, &voice->sound);
		if (r != MA_SUCCESS)
		{
			LOG_WARN(Core, "[SoundDevice] lecture impossible ({}) : {}", static_cast<int>(r), rq.path);
			return 0;
		}
		voice->loop = rq.loop;
		ma_sound_set_looping(&voice->sound, rq.loop ? MA_TRUE : MA_FALSE);
		ma_sound_set_pitch(&voice->sound, std::clamp(rq.pitch, 0.25f, 4.0f));
		if (rq.spatial)
		{
			ma_sound_set_position(&voice->sound, rq.position.x, rq.position.y, rq.position.z);
			ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_linear);
			ma_sound_set_min_distance(&voice->sound, std::max(0.01f, rq.minDistance));
			ma_sound_set_max_distance(&voice->sound, std::max(rq.minDistance + 0.01f, rq.maxDistance));
		}
		const float vol = std::max(0.0f, rq.volume);
		if (rq.fadeInMs > 0)
		{
			ma_sound_set_volume(&voice->sound, vol);
			ma_sound_set_fade_in_milliseconds(&voice->sound, 0.0f, vol, rq.fadeInMs);
		}
		else
		{
			ma_sound_set_volume(&voice->sound, vol);
		}
		if (ma_sound_start(&voice->sound) != MA_SUCCESS)
		{
			ma_sound_uninit(&voice->sound);
			return 0;
		}
		const uint32_t id = m_impl->nextVoice++;
		if (m_impl->nextVoice == 0)
			m_impl->nextVoice = 1;
		m_impl->voices.emplace(id, std::move(voice));
		return id;
	}

	void SoundDevice::SetVoiceVolume(uint32_t id, float volume, uint32_t fadeMs)
	{
		if (!IsReady())
			return;
		auto it = m_impl->voices.find(id);
		if (it == m_impl->voices.end() || it->second->stopping)
			return;
		ma_sound* s = &it->second->sound;
		const float target = std::max(0.0f, volume);
		if (fadeMs == 0)
		{
			ma_sound_set_fade_in_milliseconds(s, -1.0f, target, 0);
			ma_sound_set_volume(s, target);
		}
		else
		{
			// -1 : part du volume courant du fondu en cours.
			ma_sound_set_fade_in_milliseconds(s, -1.0f, target, fadeMs);
		}
	}

	void SoundDevice::SetVoicePosition(uint32_t id, const engine::math::Vec3& p)
	{
		if (!IsReady())
			return;
		auto it = m_impl->voices.find(id);
		if (it != m_impl->voices.end())
			ma_sound_set_position(&it->second->sound, p.x, p.y, p.z);
	}

	void SoundDevice::Stop(uint32_t id, uint32_t fadeMs)
	{
		if (!IsReady())
			return;
		auto it = m_impl->voices.find(id);
		if (it == m_impl->voices.end())
			return;
		Impl::Voice& v = *it->second;
		if (fadeMs == 0)
		{
			m_impl->FreeVoice(v);
			m_impl->voices.erase(it);
			return;
		}
		ma_sound_set_fade_in_milliseconds(&v.sound, -1.0f, 0.0f, fadeMs);
		v.stopping = true;
		v.stopAt = Clock::now() + std::chrono::milliseconds(fadeMs + 20);
	}

	bool SoundDevice::IsPlaying(uint32_t id) const
	{
		if (!IsReady())
			return false;
		auto it = m_impl->voices.find(id);
		if (it == m_impl->voices.end())
			return false;
		return !it->second->stopping && (it->second->loop || !ma_sound_at_end(&it->second->sound));
	}

	void SoundDevice::SetListener(const engine::math::Vec3& p, const engine::math::Vec3& f, const engine::math::Vec3& u)
	{
		if (!IsReady())
			return;
		ma_engine_listener_set_position(&m_impl->engine, 0, p.x, p.y, p.z);
		ma_engine_listener_set_direction(&m_impl->engine, 0, f.x, f.y, f.z);
		ma_engine_listener_set_world_up(&m_impl->engine, 0, u.x, u.y, u.z);
	}

	void SoundDevice::Update()
	{
		if (!IsReady())
			return;
		const auto now = Clock::now();
		for (auto it = m_impl->voices.begin(); it != m_impl->voices.end();)
		{
			Impl::Voice& v = *it->second;
			const bool finished = (!v.loop && ma_sound_at_end(&v.sound)) || (v.stopping && now >= v.stopAt);
			if (finished)
			{
				m_impl->FreeVoice(v);
				it = m_impl->voices.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	uint32_t SoundDevice::ActiveVoices() const
	{
		return IsReady() ? static_cast<uint32_t>(m_impl->voices.size()) : 0u;
	}

	uint64_t SoundDevice::ReadFrames(float* out, uint64_t frames)
	{
		if (!IsReady() || out == nullptr)
			return 0;
		ma_uint64 read = 0;
		if (ma_engine_read_pcm_frames(&m_impl->engine, out, frames, &read) != MA_SUCCESS)
			return 0;
		return read;
	}
}
