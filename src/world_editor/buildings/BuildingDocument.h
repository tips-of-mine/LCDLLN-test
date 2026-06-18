#pragma once

#include "src/client/world/instances/Buildings.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world::buildings
{
	/// Document éditeur portant les PLACEMENTS de bâtiments d'une zone (chacun
	/// = référence vers une variante de la bibliothèque + transform monde).
	/// CRUD + events pour les panels (Outliner, Inspector). Persiste dans
	/// `instances/zone_<zoneId>/buildings.bin` (LCBD v1) via `Buildings.h`.
	/// Calque exact de `MeshInsertDocument`.
	class BuildingDocument
	{
	public:
		using BuildingPlacement = engine::world::instances::BuildingPlacement;
		using ChangeCallback = std::function<void(const BuildingPlacement&)>;
		using RemoveCallback = std::function<void(uint64_t /*removedGuid*/)>;

		/// Définit l'identifiant (sanitizé) de la zone éditée : chemins disque
		/// `instances/zone_<zoneId>/buildings.bin`. Chaîne vide = legacy plat.
		void SetZoneId(std::string zoneId) { m_zoneId = std::move(zoneId); }
		const std::string& GetZoneId() const { return m_zoneId; }

		/// Génère un nouveau guid unique pour ce document. Thread : main thread.
		uint64_t NextGuid() { return ++m_nextGuid; }

		/// Insère le placement. Si `guid == 0`, en assigne un nouveau. Retourne
		/// le guid effectivement utilisé.
		uint64_t Add(BuildingPlacement placement);

		/// Retire le placement par guid. Retourne true si trouvé.
		bool Remove(uint64_t guid);

		/// Met à jour un placement existant (par guid). Retourne true si trouvé.
		bool Update(uint64_t guid, const BuildingPlacement& newData);

		/// Retourne un pointeur vers le placement ou nullptr. Lecture seule.
		const BuildingPlacement* GetByGuid(uint64_t guid) const;

		/// Pointeur MUTABLE ou nullptr. Marque dirty si non nul.
		BuildingPlacement* MutableByGuid(uint64_t guid);

		const std::vector<BuildingPlacement>& All() const { return m_placements; }
		size_t Size() const { return m_placements.size(); }

		bool IsDirty() const noexcept { return m_dirty; }
		void MarkDirty() noexcept     { m_dirty = true; }
		void ClearDirty() noexcept    { m_dirty = false; }

		/// Vide intégralement le document (placements + compteur de guid).
		void Reset() noexcept
		{
			m_placements.clear();
			m_nextGuid = 0u;
			m_dirty    = true;
		}

		/// Sauvegarde dans `instances/zone_<zoneId>/buildings.bin`. Reset dirty.
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);

		/// Charge depuis `instances/zone_<zoneId>/buildings.bin` (fallback legacy).
		/// Fichier absent : doc vide sans erreur. Init guid au max+1.
		bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

		void SetOnAdded(ChangeCallback cb)   { m_onAdded   = std::move(cb); }
		void SetOnUpdated(ChangeCallback cb) { m_onUpdated = std::move(cb); }
		void SetOnRemoved(RemoveCallback cb) { m_onRemoved = std::move(cb); }

	private:
		std::vector<BuildingPlacement> m_placements;
		std::string m_zoneId;
		uint64_t    m_nextGuid = 0u;
		bool        m_dirty    = false;

		ChangeCallback m_onAdded;
		ChangeCallback m_onUpdated;
		RemoveCallback m_onRemoved;
	};
}
