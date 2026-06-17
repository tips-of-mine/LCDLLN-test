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
	/// Document éditeur portant les bâtiments (grappes d'éléments) d'une zone.
	/// CRUD + events pour les panels (Outliner, Inspector). Persiste dans
	/// `instances/zone_<zoneId>/buildings.bin` (LCBD v1) via `Buildings.h`
	/// (sérialisation header-only). Calque exact de `MeshInsertDocument`
	/// (lot B3 — chemin namespacé + fallback legacy en lecture).
	///
	/// Le générateur de Guid est un compteur monotone initialisé à 1 ;
	/// le guid 0 reste réservé comme sentinelle "absent".
	class BuildingDocument
	{
	public:
		using BuildingInstance = engine::world::instances::BuildingInstance;
		using ChangeCallback = std::function<void(const BuildingInstance&)>;
		using RemoveCallback = std::function<void(uint64_t /*removedGuid*/)>;

		/// Définit l'identifiant (sanitizé) de la zone éditée : les chemins
		/// disque deviennent `instances/zone_<zoneId>/buildings.bin`. Chaîne
		/// vide = chemins legacy plats (tests, boot). À appeler à chaque
		/// changement de carte, AVANT Save/LoadFromDisk.
		void SetZoneId(std::string zoneId) { m_zoneId = std::move(zoneId); }

		/// Identifiant de zone courant ("" = chemins legacy plats).
		const std::string& GetZoneId() const { return m_zoneId; }

		/// Génère un nouveau guid unique pour ce document. Thread : main thread.
		uint64_t NextGuid() { return ++m_nextGuid; }

		/// Insère le bâtiment. Si `building.guid == 0`, en assigne un nouveau.
		/// Retourne le guid effectivement utilisé.
		uint64_t Add(BuildingInstance building);

		/// Retire le bâtiment par guid. Retourne true si trouvé.
		bool Remove(uint64_t guid);

		/// Met à jour un bâtiment existant (par guid). Retourne true si trouvé.
		bool Update(uint64_t guid, const BuildingInstance& newData);

		/// Retourne un pointeur vers le bâtiment ou nullptr. Lecture seule.
		const BuildingInstance* GetByGuid(uint64_t guid) const;

		/// Pointeur MUTABLE vers le bâtiment ou nullptr. Marque dirty si non nul
		/// (l'appelant est censé modifier le contenu).
		BuildingInstance* MutableByGuid(uint64_t guid);

		const std::vector<BuildingInstance>& All() const { return m_buildings; }
		size_t Size() const { return m_buildings.size(); }

		bool IsDirty() const noexcept { return m_dirty; }
		void MarkDirty() noexcept     { m_dirty = true; }
		void ClearDirty() noexcept    { m_dirty = false; }

		/// Vide intégralement le document (bâtiments + compteur de guid).
		void Reset() noexcept
		{
			m_buildings.clear();
			m_nextGuid = 0u;
			m_dirty    = true;
		}

		/// Sauvegarde dans `<paths.content>/instances/zone_<zoneId>/buildings.bin`
		/// (chemin plat legacy si zoneId vide). Reset `m_dirty`.
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);

		/// Charge depuis `<paths.content>/instances/zone_<zoneId>/buildings.bin`
		/// (fallback LECTURE sur le chemin plat legacy). Fichier absent : doc
		/// vide sans erreur. Initialise le compteur Guid au max+1.
		bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

		/// Callbacks observables (Outliner / Inspector).
		void SetOnAdded(ChangeCallback cb)   { m_onAdded   = std::move(cb); }
		void SetOnUpdated(ChangeCallback cb) { m_onUpdated = std::move(cb); }
		void SetOnRemoved(RemoveCallback cb) { m_onRemoved = std::move(cb); }

	private:
		std::vector<BuildingInstance> m_buildings;
		std::string m_zoneId;
		uint64_t    m_nextGuid = 0u;
		bool        m_dirty    = false;

		ChangeCallback m_onAdded;
		ChangeCallback m_onUpdated;
		RemoveCallback m_onRemoved;
	};
}
