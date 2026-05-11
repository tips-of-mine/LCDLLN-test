#pragma once
// CMANGOS.16 (Phase 1b) — ObjectAccessor : façade thread-safe pour lookup
// d'entités par GUID. Data-driven (EntityId opaque) — pas de hierarchie OOP.

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::server::shard::globals
{
	/// Type opaque pour les entités du shard. Aligné avec engine::server::EntityId
	/// (réutiliser via include indirect au moment de l'intégration).
	using EntityId = uint64_t;

	/// Snapshot d'une entité accessible : metadata légère, pas le pointeur runtime.
	/// Le caller utilise cet `EntityId` pour interroger les systèmes spécifiques
	/// (positions via `SpatialPartition`, etc.).
	struct EntitySnapshot
	{
		EntityId entityId = 0;
		std::string name;
		uint32_t mapId   = 0;
		uint32_t zoneId  = 0;
		bool isPlayer    = false;     ///< true=Player, false=Creature
	};

	/// Singleton thread-safe : registre des entités présentes côté shard.
	/// Inscription au login (Player) ou spawn (Creature). Désinscription au
	/// logout/despawn.
	///
	/// Thread-safety : `std::shared_mutex` — multiple readers concurrents,
	/// writer exclusif lors de Register/Unregister.
	class ObjectAccessor
	{
	public:
		ObjectAccessor() = default;
		~ObjectAccessor() = default;
		ObjectAccessor(const ObjectAccessor&) = delete;
		ObjectAccessor& operator=(const ObjectAccessor&) = delete;

		/// Inscrit une entité au registre. Si déjà présente, écrase (utile pour
		/// reconnexion après crash). Retourne false uniquement si entityId==0.
		bool Register(const EntitySnapshot& snapshot);

		/// Désinscrit une entité. Retourne true si l'entité était présente.
		bool Unregister(EntityId entityId);

		/// Lookup par GUID. Retourne un snapshot copié (thread-safe lecture).
		std::optional<EntitySnapshot> Find(EntityId entityId) const;

		/// Lookup par nom normalisé (lowercase). Retourne le premier match.
		/// Lent : O(N). Pour cas peu fréquents (whisper par nom). Utilise
		/// `SessionCharacterMap` pour les hot paths existants.
		std::optional<EntitySnapshot> FindByName(std::string_view name) const;

		/// Nombre d'entrées actuellement enregistrées.
		size_t Size() const;

	private:
		mutable std::shared_mutex m_mutex;
		std::unordered_map<EntityId, EntitySnapshot> m_entities;
	};
}
