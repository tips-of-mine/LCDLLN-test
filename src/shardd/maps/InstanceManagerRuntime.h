#pragma once
// Wave 9 — Wrapper runtime InstanceManager : detient un registre de
// MapIds "instanciables" seede au boot du shardd avec 2-3 maps hardcodees
// (deux donjons et un BG) + un InstanceManager pour le lifecycle reel
// (Create / Touch / MarkIdle / Despawn / GarbageCollect). V1 : map
// registry in-memory, pas de validation cross-DBC. Future iteration :
// SeedFromDb() qui charge depuis la table map_template avec capacite,
// idle_unload_ms, party_size_max, etc.
//
// Pas de Tick() pour cette PR : la creation d'instance se fait a la
// demande (event d'entree de donjon ou ouverture de BG). Un futur tick
// sera ajoute pour GarbageCollect() periodique des instances Idle, mais
// pour V1 on prouve juste que le path "registre + Create" est cable.

#include "src/shardd/maps/InstanceManager.h"

#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace engine::server::maps
{
	/// Wrapper minimaliste autour d'InstanceManager + un set de MapIds
	/// "instanciables". Le set sert de garde-fou : on refuse de creer
	/// une instance pour une map non enregistree (loader DB fournira
	/// l'ensemble canonique). InstanceManager reste expose pour les
	/// operations avancees (TouchActivity, MarkIdle, GarbageCollect).
	class InstanceManagerRuntime
	{
	public:
		InstanceManagerRuntime() = default;

		/// Enregistre 2-3 MapIds V1 hardcodees comme instanciables. Voir
		/// .cpp pour les valeurs. Idempotent : peut etre rappele pour
		/// reset le registre (utile en tests, jamais en prod ; reset le
		/// registre n'affecte pas les instances deja creees).
		void SeedV1Maps();

		/// Cree une nouvelle instance pour \p mapId si \p mapId est
		/// enregistre. Retourne l'InstanceId alloue, ou 0 si la map est
		/// inconnue (0 n'est jamais un ID valide vu que m_mgr.m_nextId
		/// demarre a 1).
		///
		/// \param partyGuid Identifiant de party demandeuse, conserve
		///   pour traces futures. V1 : non stocke, le binding party ->
		///   instance sera ajoute en meme temps que le PartyManager.
		InstanceId CreateInstance(MapId mapId, std::uint64_t partyGuid, std::uint64_t nowMs);

		/// True si \p mapId est dans le registre des maps instanciables.
		bool IsMapRegistered(MapId mapId) const noexcept;

		/// Nombre de MapIds enregistres (sert pour le log boot
		/// "[InstanceManager] N maps registered at boot").
		std::size_t MapCount() const noexcept { return m_maps.size(); }

		/// Nombre d'instances actuellement vivantes (Created/Active/Idle).
		/// Les instances Despawned restent comptees jusqu'a un GC.
		std::size_t LiveInstanceCount() const noexcept { return m_mgr.Size(); }

		/// Accesseur sur le manager interne pour les operations avancees
		/// (TouchActivity, MarkIdle, Despawn, GarbageCollect, Find). A
		/// utiliser parcimonieusement : preferable d'enrichir l'API du
		/// runtime quand un cas d'usage stable emerge.
		InstanceManager&       Manager() noexcept       { return m_mgr; }
		const InstanceManager& Manager() const noexcept { return m_mgr; }

	private:
		std::unordered_set<MapId> m_maps;
		InstanceManager           m_mgr;
	};
}
