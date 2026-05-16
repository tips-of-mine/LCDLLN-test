// src/world_editor/water/WaterDocument.h
#pragma once

#include "src/client/world/water/WaterSurfaces.h"
#include "src/world_editor/water/OceanSettings.h"

#include <string>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	/// État des plans d'eau de la zone éditée (M100.13, étendu M100.36).
	/// Persiste dans `<paths.content>/instances/water.bin` (format v2). Un
	/// seul WaterScene par éditeur (chunk-level partitioning vient avec
	/// M100.34).
	///
	/// M100.36 introduit `OceanSettings` comme membre interne : la valeur de
	/// `seaLevelMeters` lue par tous les outils (watershed, futurs coastline /
	/// erosions) vient d'ici, jamais d'un buffer local. Voir spec ticket
	/// "Contexte critique §5 — source de vérité unique pour seaLevelMeters".
	class WaterDocument
	{
	public:
		/// Accès mutable à la scene (modification par les outils de l'éditeur).
		engine::world::water::WaterScene&       Mutable()       { return m_scene; }
		/// Accès lecture seule à la scene (rendu, sérialisation).
		const engine::world::water::WaterScene& Get()     const { return m_scene; }

		/// M100.36 — Accès lecture seule à l'`OceanSettings` global de la zone.
		/// Tout consommateur du sea level (watershed, coastline, erosions)
		/// passe par cet accesseur, jamais par un buffer local. La valeur
		/// par défaut est `OceanSettings{}.seaLevelMeters = 50` quand la
		/// zone n'a jamais été sauvée.
		const OceanSettings& GetOcean() const noexcept { return m_ocean; }

		/// M100.36 — Mute l'`OceanSettings`. Marque le doc dirty pour qu'un
		/// `SaveToDisk` ultérieur persiste la valeur. Aucune validation : un
		/// sea level négatif ou aberrant est légal (responsabilité de l'UI
		/// de bornifier).
		void SetOceanSettings(const OceanSettings& ocean) noexcept
		{
			m_ocean = ocean;
			m_dirty = true;
		}

		bool IsDirty() const noexcept { return m_dirty; }
		void MarkDirty() noexcept     { m_dirty = true; }
		/// M100.14 — Reset le flag dirty sans toucher au contenu (utilise par
		/// Engine::Render apres avoir reconstruit les buffers GPU water depuis la scene).
		void ClearDirty() noexcept    { m_dirty = false; }

		/// M100.46 — Vide intégralement la scène (lakes, rivers, océan
		/// par défaut). Marque `m_dirty`. Utilisé par `WorldMapEditDocumentReset`
		/// avant l'exécution d'un zone preset sur une zone non vide.
		void Reset() noexcept
		{
			m_scene = engine::world::water::WaterScene{};
			m_ocean = OceanSettings{};
			m_dirty = true;
		}

		/// Sauvegarde dans `<paths.content>/instances/water.bin`. Reset m_dirty.
		/// Écrit toujours en format v2 (avec `m_ocean.seaLevelMeters`).
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);

		/// Charge depuis `<paths.content>/instances/water.bin`. Accepte v1
		/// (sans section ocean → `m_ocean` reste au défaut) ou v2. Si fichier
		/// absent, retourne true avec scene vide (premier lancement) et
		/// `m_ocean` au défaut. Reset m_dirty.
		bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

	private:
		engine::world::water::WaterScene m_scene;
		OceanSettings                    m_ocean;  // M100.36
		bool m_dirty = false;
	};
}
