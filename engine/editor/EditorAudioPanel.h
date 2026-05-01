#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor
{
	/// M43.5 — Panneau ImGui d'édition de `game/data/audio/zone_audio.json`.
	///
	/// Charge le fichier JSON via `engine::core::Config::LoadFromFile` (même chemin que
	/// `AudioEngine::LoadZoneAudio`), expose les 3 sections (buses / sounds / zones) en
	/// CRUD via 3 onglets ImGui, sauvegarde via sérialisation JSON manuelle.
	///
	/// Windows uniquement (utilise ImGui qui n'est lié qu'au build Windows). Sur Linux,
	/// la classe se compile mais `Draw()` est un no-op.
	class EditorAudioPanel final
	{
	public:
		struct Bus
		{
			std::string id;
			float       volume = 1.0f;
		};

		struct Sound
		{
			std::string id;
			std::string path;
			std::string busId;
			float       minDistance = 1.0f;
			float       maxDistance = 25.0f;
			bool        loop = false;
		};

		struct Zone
		{
			uint32_t    zoneId = 0;
			std::string soundId;
			float       position[3] { 0.0f, 0.0f, 0.0f };
		};

		/// Charge `audio.zone_audio_path` (défaut `audio/zone_audio.json`) depuis paths.content.
		/// \return true si le fichier existe et est parseable, false sinon (panneau ouvre vide).
		bool Load(const engine::core::Config& cfg);

		/// Sérialise les structures en JSON et écrit sur disque (écrase l'existant).
		/// \return true sur succès.
		bool Save(const engine::core::Config& cfg);

		/// Dessine les 3 onglets ImGui. À appeler entre `ImGui::Begin("Audio Editor")`
		/// et `ImGui::End()` côté caller. No-op sur Linux.
		void Draw();

		bool IsDirty() const  { return m_dirty; }
		bool IsLoaded() const { return m_loaded; }

	private:
		void DrawBusesTab();
		void DrawSoundsTab();
		void DrawZonesTab();

		std::vector<Bus>   m_buses;
		std::vector<Sound> m_sounds;
		std::vector<Zone>  m_zones;
		std::string        m_resolvedPath;
		bool               m_dirty = false;
		bool               m_loaded = false;
	};
}
