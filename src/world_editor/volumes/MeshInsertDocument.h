#pragma once

#include "src/world_editor/volumes/MeshInsertInstance.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world::volumes
{
	/// Document éditeur portant les instances de mesh inserts (M100.40).
	/// CRUD + events pour les panels (Outliner, Inspector). Persiste dans
	/// `instances/zone_<zoneId>/mesh_inserts.bin` via `MeshInsertIo` (lot B3
	/// — chemin plat legacy en fallback de lecture / zoneId vide).
	///
	/// Le générateur de Guid est un compteur monotone initialisé à 1 ;
	/// `kInvalidMeshInsertGuid` (0) reste réservé comme sentinelle.
	class MeshInsertDocument
	{
	public:
		using ChangeCallback = std::function<void(const MeshInsertInstance&)>;
		using RemoveCallback = std::function<void(uint64_t /*removedGuid*/)>;

		/// Lot B3 (audit 2026-06-10 §4.2) — Définit l'identifiant (sanitizé)
		/// de la zone éditée : les chemins disque deviennent
		/// `instances/zone_<zoneId>/mesh_inserts.bin` (écriture toujours
		/// namespacée ; lecture avec fallback sur l'ancien chemin plat).
		/// Chaîne vide = chemins legacy plats (tests, boot). À appeler à
		/// chaque changement de carte, AVANT Save/LoadFromDisk.
		void SetZoneId(std::string zoneId) { m_zoneId = std::move(zoneId); }

		/// Identifiant de zone courant ("" = chemins legacy plats).
		const std::string& GetZoneId() const { return m_zoneId; }

		/// Génère un nouveau guid unique pour cette instance de document.
		/// Thread-safe : non (main thread).
		uint64_t NextGuid() { return ++m_nextGuid; }

		/// Insère l'instance. Si `instance.guid == 0`, en assigne un nouveau.
		/// Retourne le guid effectivement utilisé (ré-écrit dans `instance`
		/// par référence si nouveau).
		uint64_t Add(MeshInsertInstance instance);

		/// Retire l'instance par guid. Retourne true si trouvée.
		bool Remove(uint64_t guid);

		/// Met à jour une instance existante (par guid). Retourne true si trouvée.
		bool Update(uint64_t guid, const MeshInsertInstance& newData);

		/// Retourne un pointeur vers l'instance ou nullptr. Lecture seule.
		const MeshInsertInstance* GetByGuid(uint64_t guid) const;

		/// Renvoie les instances filtrées par `insertCategory` (ex: "cave").
		std::vector<MeshInsertInstance> GetByCategory(const std::string& category) const;

		const std::vector<MeshInsertInstance>& All() const { return m_instances; }
		size_t Size() const { return m_instances.size(); }

		bool IsDirty() const noexcept { return m_dirty; }
		void MarkDirty() noexcept     { m_dirty = true; }
		void ClearDirty() noexcept    { m_dirty = false; }

		/// M100.46 — Vide intégralement le document (instances + compteur
		/// de guid). Marque `m_dirty`. Utilisé par `WorldMapEditDocumentReset`
		/// avant l'exécution d'un zone preset sur une zone non vide.
		void Reset() noexcept
		{
			m_instances.clear();
			m_nextGuid = 0u;
			m_dirty    = true;
		}

		/// Sauvegarde dans `<paths.content>/instances/zone_<zoneId>/mesh_inserts.bin`
		/// (chemin plat legacy si zoneId vide — lot B3). Reset `m_dirty`.
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);

		/// Charge depuis `<paths.content>/instances/zone_<zoneId>/mesh_inserts.bin`
		/// (fallback LECTURE sur le chemin plat legacy si le fichier namespacé
		/// n'existe pas — lot B3). Si
		/// fichier absent : retourne true avec doc vide (premier lancement).
		/// Reset `m_dirty`. Initialise le compteur Guid au max+1 des
		/// instances chargées.
		bool LoadFromDisk(const engine::core::Config& cfg, std::string& outError);

		/// Callbacks observables (Outliner / Inspector). Set par les panels
		/// à l'init.
		void SetOnAdded(ChangeCallback cb)   { m_onAdded   = std::move(cb); }
		void SetOnUpdated(ChangeCallback cb) { m_onUpdated = std::move(cb); }
		void SetOnRemoved(RemoveCallback cb) { m_onRemoved = std::move(cb); }

	private:
		std::vector<MeshInsertInstance> m_instances;
		/// Lot B3 — identifiant (sanitizé) de la zone, namespace des chemins.
		std::string m_zoneId;
		uint64_t m_nextGuid = 0u;
		bool     m_dirty    = false;

		ChangeCallback m_onAdded;
		ChangeCallback m_onUpdated;
		RemoveCallback m_onRemoved;
	};
}
