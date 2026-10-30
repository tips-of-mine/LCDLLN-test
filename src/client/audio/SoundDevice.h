#pragma once

// SND.2 — Lecture réelle des sons du jeu (miniaudio).
//
// Avant SND.2, AudioEngine ne faisait que de la comptabilité (atténuation
// calculée, aucun son joué) : seule la musique du menu sortait (MaMenuMusic).
// SoundDevice ouvre le périphérique audio (ma_engine) et joue :
//   - des sons ponctuels 2D (interface) ou 3D (pas, impacts), avec variation
//     de hauteur et de volume ;
//   - des boucles (ambiances) avec fondu d'entrée, de sortie et de volume.
// Chaque son appartient à un BUS (groupe miniaudio : Music, SFX, UI, Weather,
// Ambience…) dont le volume est réglable ; le volume général s'applique au tout.
//
// Les fichiers courts sont décodés une fois et mis en cache par le gestionnaire
// de ressources de miniaudio (MA_SOUND_FLAG_DECODE) ; les longues boucles sont
// lues en flux (MA_SOUND_FLAG_STREAM).
//
// Mode « sans périphérique » (Options::noDevice) : le mixage est lu à la main
// par ReadFrames() — utilisé par les tests (sound_device_tests) et par un
// client sans carte son (le jeu reste jouable, muet).
//
// Thread : main thread uniquement (miniaudio mixe lui-même sur son thread).

#include "src/shared/math/Math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace engine::audio
{
	/// Demande de lecture d'un son (chemin ABSOLU déjà résolu).
	struct SoundPlayRequest
	{
		std::string path;            ///< fichier audio (wav, ogg, mp3, flac)
		std::string bus;             ///< bus de destination (créé à la volée si absent)
		float volume = 1.0f;         ///< gain linéaire du son
		float pitch = 1.0f;          ///< 1 = hauteur d'origine
		bool spatial = false;        ///< true : positionné en 3D (atténuation + panoramique)
		engine::math::Vec3 position{ 0.0f, 0.0f, 0.0f }; ///< position monde (m) si spatial
		float minDistance = 1.0f;    ///< distance (m) sous laquelle le son est à plein volume
		float maxDistance = 30.0f;   ///< distance (m) au-delà de laquelle il est muet
		bool loop = false;           ///< boucle (ambiances)
		bool stream = false;         ///< lecture en flux (fichiers longs)
		uint32_t fadeInMs = 0;       ///< fondu d'entrée (ms)
	};

	class SoundDevice final
	{
	public:
		struct Options
		{
			bool noDevice = false;     ///< pas de périphérique : mixage lu par ReadFrames (tests)
			uint32_t sampleRate = 48000;
			uint32_t channels = 2;
			uint32_t maxVoices = 64;   ///< voix simultanées max (les sons ponctuels en trop sont ignorés)
		};

		SoundDevice();
		~SoundDevice();
		SoundDevice(const SoundDevice&) = delete;
		SoundDevice& operator=(const SoundDevice&) = delete;

		/// Ouvre le moteur miniaudio. \return false si aucun périphérique (le jeu reste muet).
		/// Effet de bord : ouvre le périphérique audio du système (sauf noDevice).
		bool Init(const Options& options);
		/// Arrête toutes les voix et ferme le périphérique. Idempotent.
		void Shutdown();
		bool IsReady() const;

		/// Crée (ou met à jour) un bus et son volume linéaire [0, 1].
		void SetBusVolume(std::string_view bus, float volume);
		/// Volume général [0, 1] (appliqué à tous les bus).
		void SetMasterVolume(float volume);

		/// Joue un son. \return identifiant de voix (> 0), ou 0 si refusé
		/// (fichier illisible, moteur fermé, limite de voix atteinte).
		uint32_t Play(const SoundPlayRequest& request);
		/// Volume d'une voix, avec fondu linéaire de `fadeMs` millisecondes.
		void SetVoiceVolume(uint32_t voice, float volume, uint32_t fadeMs);
		/// Déplace une voix spatialisée.
		void SetVoicePosition(uint32_t voice, const engine::math::Vec3& position);
		/// Arrête une voix (fondu de sortie `fadeMs`), puis la libère.
		void Stop(uint32_t voice, uint32_t fadeMs);
		/// Vrai tant que la voix existe et n'est pas terminée.
		bool IsPlaying(uint32_t voice) const;

		/// Oreille du joueur : position (m), direction du regard et « haut » (unitaires).
		void SetListener(const engine::math::Vec3& position, const engine::math::Vec3& forward,
			const engine::math::Vec3& up);

		/// Libère les voix terminées ou arrêtées. À appeler une fois par frame.
		void Update();
		/// Nombre de voix vivantes.
		uint32_t ActiveVoices() const;

		/// Mode noDevice : lit `frames` trames mixées (float entrelacé) dans `out`.
		/// \return trames lues.
		uint64_t ReadFrames(float* out, uint64_t frames);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
