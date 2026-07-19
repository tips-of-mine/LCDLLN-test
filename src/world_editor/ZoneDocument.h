#pragma once

// Roadmap-8 (2026-07-19) — Document des zones de gameplay de la carte
// (polygones typés M100.28). Header-only, miroir de SplineDocument.
// Créé au câblage de l'outil Zone (audit 2026-06-05, thème 1.1) : l'outil
// existait mais n'avait AUCUN conteneur persisté côté éditeur.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "src/client/world/zones/Zones.h"

namespace engine::editor::world
{
	/// Conteneur éditeur des zones de gameplay d'une carte. Possède la liste,
	/// la sérialise via le format partagé `zones.bin` (magic ZONS, versionné).
	/// Contrainte thread : main thread (comme les autres documents éditeur).
	class ZoneDocument
	{
	public:
		/// Ajoute une zone en fin de liste. \return l'index attribué.
		uint32_t Add(const engine::world::zones::GameplayZone& z)
		{
			const uint32_t id = static_cast<uint32_t>(m_zones.size());
			m_zones.push_back(z);
			return id;
		}

		/// Retire la dernière zone (Undo d'un Add — LIFO, cf. AddGameplayZoneCommand).
		void RemoveLast() { if (!m_zones.empty()) m_zones.pop_back(); }

		/// Vide toutes les zones (changement de carte).
		void Clear() { m_zones.clear(); }

		const std::vector<engine::world::zones::GameplayZone>& All() const { return m_zones; }
		std::vector<engine::world::zones::GameplayZone>& Mutable() { return m_zones; }

		/// Écrit `zones.bin` au chemin donné (format partagé SaveZonesBin).
		/// Effet de bord : écriture disque (le dossier parent doit exister).
		bool SaveToDisk(const std::string& path, std::string& err) const
		{
			const std::vector<uint8_t> bytes = engine::world::zones::SaveZonesBin(m_zones);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out.good()) { err = "ZoneDocument::SaveToDisk: open failed: " + path; return false; }
			out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			if (!out.good()) { err = "ZoneDocument::SaveToDisk: write failed: " + path; return false; }
			return true;
		}

		/// Roadmap-8 (dette audit 7.2) — Relit `zones.bin` et REMPLACE le
		/// contenu courant. Fichier absent = succès avec liste vide (carte
		/// sans zones). \return false si le fichier existe mais est corrompu.
		bool LoadFromDisk(const std::string& path, std::string& err)
		{
			m_zones.clear();
			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) return true; // pas de zones : OK
			std::ifstream in(path, std::ios::binary);
			if (!in.good()) { err = "ZoneDocument::LoadFromDisk: open failed: " + path; return false; }
			std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
				std::istreambuf_iterator<char>());
			return engine::world::zones::LoadZonesBin(bytes, m_zones, err);
		}

	private:
		std::vector<engine::world::zones::GameplayZone> m_zones;
	};
}
