// src/world_editor/WaterDocument.h
#pragma once

#include "src/client/world/water/WaterSurfaces.h"

#include <string>

namespace engine::core { class Config; }

namespace engine::editor::world
{
	/// État des plans d'eau de la zone éditée (M100.13). Persiste dans
	/// `<paths.content>/instances/water.bin`. Un seul WaterScene par éditeur
	/// (chunk-level partitioning vient avec M100.34).
	class WaterDocument
	{
	public:
		/// Accès mutable à la scene (modification par les outils de l'éditeur).
		engine::world::water::WaterScene&       Mutable()       { return m_scene; }
		/// Accès lecture seule à la scene (rendu, sérialisation).
		const engine::world::water::WaterScene& Get()     const { return m_scene; }

		bool IsDirty() const noexcept { return m_dirty; }
		void MarkDirty() noexcept     { m_dirty = true; }
		/// M100.14 — Reset le flag dirty sans toucher au contenu (utilise par
		/// Engine::Render apres avoir reconstruit les buffers GPU water depuis la scene).
		void ClearDirty() noexcept    { m_dirty = false; }

		/// Sauvegarde dans `<paths.content>/instances/water.bin`. Reset m_dirty.
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);

		/// Charge depuis `<paths.content>/instances/water.bin`. Si fichier absent,
		/// retourne true avec scene vide (premier lancement). Reset m_dirty.
		bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

	private:
		engine::world::water::WaterScene m_scene;
		bool m_dirty = false;
	};
}
