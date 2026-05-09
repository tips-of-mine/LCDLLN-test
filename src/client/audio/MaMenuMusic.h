#pragma once

#include <filesystem>
#include <memory>

namespace engine::audio
{
	/// Lecture musique menu / auth en boucle (miniaudio, fichier WAV/MP3/FLAC selon build miniaudio).
	class MaMenuMusic final
	{
	public:
		MaMenuMusic();
		~MaMenuMusic();
		MaMenuMusic(const MaMenuMusic&) = delete;
		MaMenuMusic& operator=(const MaMenuMusic&) = delete;

		bool PlayLoop(const std::filesystem::path& filePathUtf8);
		void Stop();
		void SetLinearGain(float gain01);
		bool IsActive() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
