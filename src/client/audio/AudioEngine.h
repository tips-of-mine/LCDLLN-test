#pragma once

// Audio du client : catalogue des sons (game/data/audio/zone_audio.json), bus de
// volume, ambiance de zone, musique du menu.
//
// SND.2 — les sons sont réellement joués (SoundDevice, miniaudio) :
//   - Play3D : son spatialisé (pas, impacts) ;
//   - Play2D : son d'interface, sans position ;
//   - PlayLoop / SetSoundVolume / StopSound : boucles avec fondus (ambiances).
// Un son peut avoir plusieurs VARIANTES (fichiers) tirées au hasard sans
// répéter la précédente, avec une légère variation de hauteur et de volume :
// un même pas joué 50 fois ne sonne pas mécanique.
//
// Sans périphérique audio (serveur de test, pas de carte son), le catalogue
// est chargé et les appels sont sans effet : le jeu reste jouable, muet.
//
// Thread : main thread.

#include "src/shared/core/Config.h"
#include "src/shared/math/Math.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::audio { class MaMenuMusic; class SoundDevice; }

namespace engine::audio
{
	/// One named audio bus with a normalized volume multiplier.
	struct AudioBusDefinition
	{
		std::string id;
		float volume = 1.0f;
	};

	/// Un son du catalogue (zone_audio.json).
	struct AudioSoundDefinition
	{
		std::string id;
		std::string relativePath;            ///< fichier principal (variante 0)
		std::vector<std::string> variants;   ///< SND.2 — fichiers alternatifs (relatifs au contenu)
		std::string busId;
		float minDistanceMeters = 1.0f;
		float maxDistanceMeters = 25.0f;
		bool loop = false;
		float volume = 1.0f;                 ///< SND.2 — gain de base
		float pitchJitter = 0.0f;            ///< SND.2 — variation de hauteur ±(fraction), ex. 0.06
		float volumeJitter = 0.0f;           ///< SND.2 — variation de volume ±(fraction)
		bool stream = false;                 ///< SND.2 — lecture en flux (fichiers longs)

		/// Nombre total de fichiers (principal + variantes).
		size_t FileCount() const { return (relativePath.empty() ? 0u : 1u) + variants.size(); }
		/// Fichier n° i (0 = principal).
		const std::string& File(size_t i) const { return i == 0 && !relativePath.empty() ? relativePath : variants[i - (relativePath.empty() ? 0u : 1u)]; }
	};

	/// One zone ambience entry mapping a zone id to one looping sound.
	struct ZoneAmbienceDefinition
	{
		uint32_t zoneId = 0;
		std::string soundId;
		engine::math::Vec3 position{};
	};

	class AudioEngine final
	{
	public:
		AudioEngine();
		AudioEngine(const AudioEngine&) = delete;
		AudioEngine& operator=(const AudioEngine&) = delete;
		AudioEngine(AudioEngine&&) noexcept;
		AudioEngine& operator=(AudioEngine&&) noexcept;
		~AudioEngine();

		/// Charge le catalogue (buses, sons, ambiances de zone) et ouvre le
		/// périphérique audio (`audio.sfx_device_enabled`, défaut true).
		/// Effet de bord : ouvre le périphérique audio du système.
		bool Init(const engine::core::Config& config);

		/// Arrête tous les sons, ferme le périphérique, vide le catalogue.
		void Shutdown();

		/// Son spatialisé à `position` (m). `volumeScale` multiplie le volume du
		/// catalogue. \return identifiant de voix (> 0) ou 0 (son inconnu, muet).
		uint32_t Play3D(const engine::math::Vec3& position, std::string_view soundId, float volumeScale = 1.0f);
		/// Son non spatialisé (interface). \return identifiant de voix ou 0.
		uint32_t Play2D(std::string_view soundId, float volumeScale = 1.0f);
		/// Boucle non spatialisée (ambiance) démarrée avec un fondu `fadeMs`.
		/// \return identifiant de voix ou 0.
		uint32_t PlayLoop(std::string_view soundId, float volume, uint32_t fadeMs);
		/// Volume d'une voix (fondu linéaire `fadeMs`).
		void SetSoundVolume(uint32_t voice, float volume, uint32_t fadeMs);
		/// Arrête une voix (fondu `fadeMs`).
		void StopSound(uint32_t voice, uint32_t fadeMs);
		/// Vrai si le son est au catalogue.
		bool HasSound(std::string_view soundId) const;
		/// Vrai si un périphérique joue réellement les sons.
		bool IsDeviceReady() const;

		/// Position de l'oreille (caméra) et vitesse (réservée à l'effet Doppler).
		bool SetListener(const engine::math::Vec3& position, const engine::math::Vec3& velocity);
		/// SND.2 — orientation de l'oreille : direction du regard et « haut » (unitaires).
		void SetListenerOrientation(const engine::math::Vec3& forward, const engine::math::Vec3& up);

		/// Volume d'un bus [0, 1] (créé s'il n'existe pas encore).
		bool SetBusVolume(std::string_view busId, float volume);
		/// Volume général [0, 1].
		bool SetMasterVolume(float volume);

		uint32_t GetCurrentZoneId() const { return m_currentZoneId; }
		/// Démarre la boucle d'ambiance déclarée pour la zone (et coupe la précédente, fondu 1,5 s).
		bool SetZone(uint32_t zoneId);

		/// Une fois par frame : libère les voix terminées, met à jour la musique du menu.
		bool Tick(float deltaSeconds);

		/// Démarre une musique menu en boucle (fichier résolu sur disque). Arrête une éventuelle piste précédente.
		bool StartMenuMusic(const std::filesystem::path& filePathUtf8);
		void StopMenuMusic();

		/// P3.2 — sons de pas par couche splat déclarés par un runtime_manifest.json v4+
		/// (zones historiques de l'éditeur) : ids « _footstep_layer_<n> » sur le bus SFX.
		/// \return true si au moins une couche a été enregistrée.
		bool RegisterFootstepSoundsFromManifest(const std::filesystem::path& manifestAbsolutePath);
		/// Id conventionnel d'un son de pas de couche splat : « _footstep_layer_<n> ».
		static std::string FootstepSoundIdForLayer(uint32_t layer);

		/// SND.2 — choix de variante : indice dans [0, count) différent de `last`
		/// (si count > 1), à partir d'un tirage uniforme `u` ∈ [0, 1). Pur (testé).
		static size_t PickVariant(size_t count, size_t last, float u);

	private:
		bool LoadZoneAudio();
		/// Prépare et joue une voix : variante, variation, chemin absolu, bus.
		uint32_t PlayInternal(const AudioSoundDefinition& def, float volumeScale, bool spatial,
			const engine::math::Vec3& position, bool loop, uint32_t fadeMs);
		float BusVolume(const std::string& id) const;

		const engine::core::Config* m_config = nullptr;
		std::unordered_map<std::string, AudioBusDefinition> m_buses;
		std::unordered_map<std::string, AudioSoundDefinition> m_sounds;
		std::unordered_map<std::string, size_t> m_lastVariant;
		std::vector<ZoneAmbienceDefinition> m_zoneAmbience;
		std::vector<uint32_t> m_zoneLoopVoices;
		engine::math::Vec3 m_listenerPosition{};
		engine::math::Vec3 m_listenerVelocity{};
		std::string m_zoneAudioRelativePath;
		uint32_t m_currentZoneId = 0;
		float m_masterVolume = 1.0f;
		bool m_initialized = false;
		std::mt19937 m_rng{ 0x4C434453u };
		std::unique_ptr<SoundDevice> m_device;
		std::unique_ptr<MaMenuMusic> m_menuMusic;
	};
}
