// SND.2 — Tests de la lecture des sons (SoundDevice en mode sans périphérique),
// du choix de variante (AudioEngine::PickVariant) et des pas (Footsteps).

#include "src/client/audio/AudioEngine.h"
#include "src/client/audio/Footsteps.h"
#include "src/client/audio/SoundDevice.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int s_failCount = 0;

	void Check(bool ok, const char* msg)
	{
		if (!ok)
		{
			++s_failCount;
			std::cerr << "[FAIL] " << msg << std::endl;
		}
	}

	void Put16(std::ofstream& f, uint16_t v) { f.put(static_cast<char>(v & 0xFF)); f.put(static_cast<char>(v >> 8)); }
	void Put32(std::ofstream& f, uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>((v >> (8 * i)) & 0xFF)); }

	/// Écrit un WAV PCM 16 bits mono 48 kHz : sinusoïde de `seconds` s.
	void WriteSineWav(const std::filesystem::path& p, float seconds, float amp)
	{
		const uint32_t n = static_cast<uint32_t>(seconds * 48000.0f);
		std::ofstream f(p, std::ios::binary);
		f.write("RIFF", 4); Put32(f, 36 + n * 2); f.write("WAVE", 4);
		f.write("fmt ", 4); Put32(f, 16); Put16(f, 1); Put16(f, 1); Put32(f, 48000); Put32(f, 96000); Put16(f, 2); Put16(f, 16);
		f.write("data", 4); Put32(f, n * 2);
		for (uint32_t i = 0; i < n; ++i)
			Put16(f, static_cast<uint16_t>(static_cast<int16_t>(amp * 32767.0f * std::sin(6.2831853f * 440.0f * i / 48000.0f))));
	}

	float Peak(engine::audio::SoundDevice& d, uint64_t frames)
	{
		std::vector<float> buf(frames * 2);
		const uint64_t got = d.ReadFrames(buf.data(), frames);
		float pk = 0.0f;
		for (uint64_t i = 0; i < got * 2; ++i)
			pk = std::max(pk, std::fabs(buf[i]));
		return pk;
	}
}

int main()
{
	using namespace engine::audio;

	// --- SoundDevice (mixage lu à la main, sans carte son) -----------------------
	{
		const auto dir = std::filesystem::temp_directory_path() / "lcdlln_sound_tests";
		std::filesystem::create_directories(dir);
		const auto wav = dir / "sine.wav";
		WriteSineWav(wav, 0.2f, 0.5f);

		SoundDevice d;
		SoundDevice::Options opt;
		opt.noDevice = true;
		opt.maxVoices = 2;
		Check(d.Init(opt), "init sans peripherique");
		SoundPlayRequest rq;
		rq.path = wav.string();
		rq.bus = "UI";
		const uint32_t v = d.Play(rq);
		Check(v != 0, "lecture d'un wav");
		Check(Peak(d, 4800) > 0.2f, "le son est audible dans le mixage");
		d.SetBusVolume("UI", 0.0f);
		Check(Peak(d, 2400) < 1e-4f, "bus a 0 : silence");
		d.SetBusVolume("UI", 1.0f);
		// Fin du son : la voix est libérée par Update.
		Peak(d, 48000);
		d.Update();
		Check(d.ActiveVoices() == 0, "voix liberee en fin de son");

		// Limite de voix : 2 sons ponctuels max, une boucle passe quand même.
		Check(d.Play(rq) != 0 && d.Play(rq) != 0, "deux voix");
		Check(d.Play(rq) == 0, "troisieme son ponctuel refuse (limite)");
		SoundPlayRequest loop = rq;
		loop.loop = true;
		const uint32_t lv = d.Play(loop);
		Check(lv != 0, "une boucle passe malgre la limite");
		d.Stop(lv, 0);
		Check(!d.IsPlaying(lv), "boucle arretee");

		// Fichier absent : refus propre.
		SoundPlayRequest bad;
		bad.path = (dir / "absent.wav").string();
		Check(d.Play(bad) == 0, "fichier absent refuse");
		d.Shutdown();
		Check(!d.IsReady(), "arret");
		std::filesystem::remove_all(dir);
	}

	// --- Variantes ---------------------------------------------------------------
	{
		Check(AudioEngine::PickVariant(1, 0, 0.7f) == 0, "une seule variante");
		bool neverRepeat = true;
		for (size_t last = 0; last < 4; ++last)
			for (float u = 0.0f; u < 1.0f; u += 0.05f)
			{
				const size_t k = AudioEngine::PickVariant(4, last, u);
				neverRepeat = neverRepeat && k != last && k < 4;
			}
		Check(neverRepeat, "jamais deux fois la meme variante d'affilee");
		Check(AudioEngine::PickVariant(4, 4, 0.99f) == 3, "premier tirage : toute la plage");
	}

	// --- Cadence des pas -----------------------------------------------------------
	{
		FootstepCadence c;
		int steps = 0;
		for (int i = 0; i < 600; ++i) // 10 s de marche à 2 m/s
			if (c.Update(1.0f / 60.0f, 2.0f, true, false) == FootstepEvent::Step)
				++steps;
		const float stride = FootstepCadence::StrideLength(2.0f);
		Check(std::abs(steps - static_cast<int>(20.0f / stride)) <= 1, "marche : un pas par foulee");
		Check(FootstepCadence::StrideLength(8.0f) > FootstepCadence::StrideLength(2.0f), "foulee plus longue en courant");

		int idle = 0;
		for (int i = 0; i < 120; ++i)
			if (c.Update(1.0f / 60.0f, 0.0f, true, false) != FootstepEvent::None)
				++idle;
		Check(idle == 0, "a l'arret : aucun pas");

		// Chute de 0,5 s puis réception.
		FootstepEvent last = FootstepEvent::None;
		for (int i = 0; i < 30; ++i)
			c.Update(1.0f / 60.0f, 3.0f, false, false);
		last = c.Update(1.0f / 60.0f, 3.0f, true, false);
		Check(last == FootstepEvent::Land, "reception apres une chute");
		// Petit saut (0,1 s) : pas de bruit de réception.
		for (int i = 0; i < 6; ++i)
			c.Update(1.0f / 60.0f, 3.0f, false, false);
		Check(c.Update(1.0f / 60.0f, 3.0f, true, false) != FootstepEvent::Land, "petit saut : pas de reception");

		// Nage : une brasse toutes les kStrokeSeconds.
		FootstepCadence s;
		int strokes = 0;
		for (int i = 0; i < 600; ++i)
			if (s.Update(1.0f / 60.0f, 1.5f, false, true) == FootstepEvent::Stroke)
				++strokes;
		Check(strokes == static_cast<int>(10.0f / FootstepCadence::kStrokeSeconds), "nage : brasses regulieres");
	}

	// --- Surface → son --------------------------------------------------------------
	{
		FootstepContext ctx;
		ctx.layer = GroundLayer::Grass;
		Check(FootstepSoundId(ctx) == "step_grass", "herbe");
		ctx.layer = GroundLayer::Snow;
		Check(FootstepSoundId(ctx) == "step_snow_crunch", "neige");
		ctx.heightAboveTerrain = 1.0f;
		Check(FootstepSoundId(ctx) == "step_wood", "plancher");
		ctx.inShallowWater = true;
		Check(FootstepSoundId(ctx) == "step_water", "eau peu profonde");
		ctx.swimming = true;
		Check(FootstepSoundId(ctx) == "swim_stroke", "nage");
		Check(FootstepSoundId(FootstepContext{}) == "step_dirt", "sol inconnu : terre");
		Check(FootstepVolume(1.0f) < FootstepVolume(7.0f), "la course s'entend plus que la marche");
	}

	if (s_failCount == 0)
		std::cout << "[OK] sound_tests" << std::endl;
	return s_failCount == 0 ? 0 : 1;
}
