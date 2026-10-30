#include "src/client/audio/MaMenuMusic.h"

#include "src/shared/core/Log.h"

#include <algorithm>
#include <cstring>

// SND.1 — décodeur Ogg Vorbis (stb_vorbis, domaine public, external/stb) :
// miniaudio ne lit l'Ogg Vorbis que si stb_vorbis est déclaré AVANT son
// implémentation, puis compilé après (procédure documentée par miniaudio).
// Musiques et ambiances longues sont livrées en .ogg (boucle exacte à
// l'échantillon, contrairement au MP3 et à son délai d'encodeur).
#if defined(_MSC_VER)
#pragma warning(push, 0) // en-tetes tiers : silence les warnings
#endif
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace engine::audio
{
	struct MaMenuMusic::Impl
	{
		ma_engine engine{};
		ma_sound sound{};
		bool engineReady = false;
		bool soundReady = false;
	};

	MaMenuMusic::MaMenuMusic()
		: m_impl(std::make_unique<Impl>())
	{
	}

	MaMenuMusic::~MaMenuMusic()
	{
		Stop();
	}

	bool MaMenuMusic::PlayLoop(const std::filesystem::path& filePathUtf8)
	{
		Stop();
		if (filePathUtf8.empty())
		{
			return false;
		}

		const std::string pathStr = filePathUtf8.string();
		ma_result er = ma_engine_init(nullptr, &m_impl->engine);
		if (er != MA_SUCCESS)
		{
			LOG_ERROR(Core, "[MaMenuMusic] ma_engine_init failed ({})", static_cast<int>(er));
			return false;
		}
		m_impl->engineReady = true;

		er = ma_sound_init_from_file(&m_impl->engine, pathStr.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &m_impl->sound);
		if (er != MA_SUCCESS)
		{
			LOG_ERROR(Core, "[MaMenuMusic] ma_sound_init_from_file failed ({}) path={}", static_cast<int>(er), pathStr);
			ma_engine_uninit(&m_impl->engine);
			m_impl->engineReady = false;
			std::memset(&m_impl->engine, 0, sizeof(m_impl->engine));
			return false;
		}
		m_impl->soundReady = true;

		ma_sound_set_looping(&m_impl->sound, MA_TRUE);
		ma_sound_set_volume(&m_impl->sound, 1.0f);
		er = ma_sound_start(&m_impl->sound);
		if (er != MA_SUCCESS)
		{
			LOG_ERROR(Core, "[MaMenuMusic] ma_sound_start failed ({})", static_cast<int>(er));
			Stop();
			return false;
		}

		LOG_INFO(Core, "[MaMenuMusic] Looping music started ({})", pathStr);
		return true;
	}

	void MaMenuMusic::Stop()
	{
		if (!m_impl)
		{
			return;
		}
		if (m_impl->soundReady)
		{
			ma_sound_stop(&m_impl->sound);
			ma_sound_uninit(&m_impl->sound);
			m_impl->soundReady = false;
			std::memset(&m_impl->sound, 0, sizeof(m_impl->sound));
		}
		if (m_impl->engineReady)
		{
			ma_engine_uninit(&m_impl->engine);
			m_impl->engineReady = false;
			std::memset(&m_impl->engine, 0, sizeof(m_impl->engine));
		}
	}

	void MaMenuMusic::SetLinearGain(float gain01)
	{
		if (!m_impl || !m_impl->soundReady)
		{
			return;
		}
		const float g = std::clamp(gain01, 0.0f, 1.0f);
		ma_sound_set_volume(&m_impl->sound, g);
	}

	bool MaMenuMusic::IsActive() const
	{
		return m_impl && m_impl->soundReady;
	}
}
