// SND.1 — Garde-fou des fichiers audio livrés (exécuté par la CI, depuis la racine du dépôt).
//
// Vérifie que la musique de l'écran de connexion déclarée dans config.json
// (audio.menu_music_path) et dans game/data/audio/zone_audio.json (auth_music)
// existe, est décodable par miniaudio (Ogg Vorbis via stb_vorbis, compilé dans
// engine_core — MaMenuMusic.cpp) et respecte le cahier des charges : 48 kHz,
// stéréo, entre 5 et 10 minutes. SND.2 : tous les fichiers déclarés dans
// zone_audio.json (sons et variantes) existent et se décodent.

#include "src/shared/core/Config.h"

#include "miniaudio.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const std::string& msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}
}

int main()
{
	engine::core::Config cfg;
	Check(cfg.LoadFromFile("config.json"), "config.json lisible");
	const std::string menuRel = cfg.GetString("audio.menu_music_path", "");
	Check(!menuRel.empty(), "audio.menu_music_path renseigne");

	engine::core::Config zoneAudio;
	Check(zoneAudio.LoadFromFile("game/data/audio/zone_audio.json"), "zone_audio.json lisible");
	bool authFound = false;
	for (int i = 0; i < 64; ++i)
	{
		const std::string base = "sounds[" + std::to_string(i) + "].";
		if (!zoneAudio.Has(base + "id"))
			break;
		if (zoneAudio.GetString(base + "id", "") == "auth_music")
		{
			authFound = true;
			Check(zoneAudio.GetString(base + "path", "") == menuRel, "auth_music et audio.menu_music_path designent la meme piste");
		}
	}
	Check(authFound, "son auth_music declare dans zone_audio.json");

	const std::filesystem::path file = std::filesystem::path("game/data") / menuRel;
	Check(std::filesystem::exists(file), "fichier present : " + file.string());
	if (std::filesystem::exists(file))
	{
		ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0);
		ma_decoder dec;
		const ma_result r = ma_decoder_init_file(file.string().c_str(), &dc, &dec);
		Check(r == MA_SUCCESS, "decodable par miniaudio (Ogg Vorbis active) : code " + std::to_string(static_cast<int>(r)));
		if (r == MA_SUCCESS)
		{
			ma_uint64 frames = 0;
			Check(ma_decoder_get_length_in_pcm_frames(&dec, &frames) == MA_SUCCESS, "longueur connue");
			const double seconds = dec.outputSampleRate ? static_cast<double>(frames) / dec.outputSampleRate : 0.0;
			Check(dec.outputSampleRate == 48000u, "48 kHz");
			Check(dec.outputChannels == 2u, "stereo");
			Check(seconds >= 300.0 && seconds <= 600.0, "duree entre 5 et 10 min : " + std::to_string(seconds) + " s");
			// Décodage effectif d'une seconde (le flux n'est pas qu'un en-tête).
			float buf[4800 * 2];
			ma_uint64 read = 0;
			Check(ma_decoder_read_pcm_frames(&dec, buf, 4800, &read) == MA_SUCCESS && read == 4800, "decodage des premieres trames");
			ma_decoder_uninit(&dec);
		}
	}

	// SND.2 — chaque fichier déclaré dans zone_audio.json (principal + variantes)
	// existe et se décode.
	size_t files = 0;
	for (int i = 0; i < 1024; ++i)
	{
		const std::string base = "sounds[" + std::to_string(i) + "].";
		if (!zoneAudio.Has(base + "id"))
			break;
		const std::string id = zoneAudio.GetString(base + "id", "");
		std::vector<std::string> paths{ zoneAudio.GetString(base + "path", "") };
		for (int k = 0; k < 64 && zoneAudio.Has(base + "variants[" + std::to_string(k) + "]"); ++k)
			paths.push_back(zoneAudio.GetString(base + "variants[" + std::to_string(k) + "]", ""));
		for (const std::string& rel : paths)
		{
			const std::filesystem::path f = std::filesystem::path("game/data") / rel;
			++files;
			if (!std::filesystem::exists(f))
			{
				Check(false, "fichier absent : " + rel + " (son " + id + ")");
				continue;
			}
			ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, 0, 0);
			ma_decoder dec;
			const bool ok = ma_decoder_init_file(f.string().c_str(), &dc, &dec) == MA_SUCCESS;
			Check(ok, "decodable : " + rel);
			if (ok)
				ma_decoder_uninit(&dec);
		}
	}
	Check(files > 40, "catalogue des sons charge (" + std::to_string(files) + " fichiers)");

	if (s_failCount == 0)
		std::cout << "[OK] audio_assets_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
