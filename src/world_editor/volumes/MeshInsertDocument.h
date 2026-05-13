#pragma once

#include "src/world_editor/volumes/MeshInsertInstance.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::core { class Config; }

namespace engine::editor::world::volumes
{
	/// Document éditeur portant les instances de mesh inserts (M100.40).
	/// CRUD + events pour les panels (Outliner, Inspector). Persiste dans
	/// `instances/mesh_inserts.bin` via `MeshInsertIo`.
	///
	/// Le générateur de Guid est un compteur monotone initialisé à 1 ;
	/// `kInvalidMeshInsertGuid` (0) reste réservé comme sentinelle.
	class MeshInsertDocument
	{
	public:
		using ChangeCallback = std::function<void(const MeshInsertInstance&)>;
		using RemoveCallback = std::function<void(uint64_t /*removedGuid*/)>;

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

		/// Sauvegarde dans `<paths.content>/instances/mesh_inserts.bin`. Reset
		/// `m_dirty`.
		bool SaveToDisk(const engine::core::Config& cfg, std::string& outError);

		/// Charge depuis `<paths.content>/instances/mesh_inserts.bin`. Si
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
		uint64_t m_nextGuid = 0u;
		bool     m_dirty    = false;

		ChangeCallback m_onAdded;
		ChangeCallback m_onUpdated;
		RemoveCallback m_onRemoved;
	};
}
