#pragma once
// HostileRefManager : pattern bidirectionnel pour les relations hostiles
// entre Unit. Complete ThreatList (Wave 8 #576) qui ne gere QUE la
// direction "qui me hait".
//
// Pour une paire hostile A -> B :
//   - A.m_targets contient B (les Unit que A hait, ses cibles offensives)
//   - B.m_haters  contient A (les Unit qui haïssent B, ses attaquants)
//
// Les 2 listes doivent rester coherentes : si A meurt, A doit etre retire
// de tous les m_targets de ses haters ET de tous les m_haters de ses
// targets. Sans ce cleanup, des "ghost references" subsistent et causent
// des bugs aggro persistant apres mort.
//
// Conception : HostileRefManager est une COMPOSANTE d'Unit, pas un
// singleton global. Chaque Unit en possede un. La synchronisation
// bidirectionnelle est faite via le helper EngageHostile(actor, target)
// ci-dessous, ou explicitement par le caller.
//
// Cleanup symetrique : CleanupOnDeath(deadId, managers) — le caller
// passe la liste des managers a nettoyer (typiquement tous les Unit du
// shard, recuperes via ObjectAccessor Wave 19 livre).

#include "src/shared/network/ReplicationTypes.h"

#include <cstddef>
#include <unordered_set>
#include <vector>

namespace engine::server::combat
{
	/// Composante "relations hostiles" d'une Unit. Detient les 2 cotes
	/// de la relation : ses cibles (m_targets) et ses attaquants (m_haters).
	class HostileRefManager
	{
	public:
		HostileRefManager() = default;

		/// Ajoute \p targetId aux cibles de cette Unit. No-op si deja present.
		/// Side note : pour synchroniser, le caller doit aussi appeler
		/// AddHater(thisOwnerId) sur target.HostileRefManager.
		void AddTarget(EntityId targetId) { m_targets.insert(targetId); }

		/// Ajoute \p haterId aux attaquants de cette Unit. No-op si deja present.
		void AddHater(EntityId haterId) { m_haters.insert(haterId); }

		/// Retire \p targetId. No-op si absent.
		void RemoveTarget(EntityId targetId) { m_targets.erase(targetId); }

		/// Retire \p haterId. No-op si absent.
		void RemoveHater(EntityId haterId) { m_haters.erase(haterId); }

		/// True si \p id est une cible offensive (cette Unit la hait).
		bool HasTarget(EntityId id) const noexcept { return m_targets.count(id) > 0; }

		/// True si \p id est un attaquant (cette Unit est haie par lui).
		bool HasHater(EntityId id) const noexcept { return m_haters.count(id) > 0; }

		const std::unordered_set<EntityId>& Targets() const noexcept { return m_targets; }
		const std::unordered_set<EntityId>& Haters() const noexcept { return m_haters; }

		size_t TargetCount() const noexcept { return m_targets.size(); }
		size_t HaterCount() const noexcept { return m_haters.size(); }

		/// True si la Unit est engagee dans une relation hostile (cible ou attaquant).
		bool IsEngaged() const noexcept { return !m_targets.empty() || !m_haters.empty(); }

		/// Reset complet (typiquement appele par Unit::OnDespawn / OnReset
		/// avant un cleanup symetrique global).
		void Clear() noexcept
		{
			m_targets.clear();
			m_haters.clear();
		}

	private:
		std::unordered_set<EntityId> m_targets;
		std::unordered_set<EntityId> m_haters;
	};

	/// Helper : etablit une relation hostile A -> B avec synchronisation
	/// automatique des 2 cotes (idempotent).
	///
	/// \param actor   manager de l'agresseur
	/// \param actorId EntityId de l'agresseur (pour s'inscrire dans target.haters)
	/// \param target  manager de la cible
	/// \param targetId EntityId de la cible (pour s'inscrire dans actor.targets)
	inline void EngageHostile(HostileRefManager& actor, EntityId actorId,
		HostileRefManager& target, EntityId targetId)
	{
		actor.AddTarget(targetId);
		target.AddHater(actorId);
	}

	/// Helper : retire la relation A -> B symetriquement (idempotent).
	inline void DisengageHostile(HostileRefManager& actor, EntityId actorId,
		HostileRefManager& target, EntityId targetId)
	{
		actor.RemoveTarget(targetId);
		target.RemoveHater(actorId);
	}

	/// Cleanup symetrique : retire \p deadId de tous les managers fournis
	/// (a la fois cote m_targets et m_haters). Idempotent et sur si \p deadId
	/// n'est present dans aucun manager.
	///
	/// Usage typique : `Unit::Die()` collecte les manager pointers depuis
	/// les EntityId de m_haters U m_targets (via ObjectAccessor), puis appelle
	/// ce helper.
	///
	/// \param deadId EntityId de l'unit qui meurt / despawn
	/// \param affectedManagers managers qui peuvent contenir une reference a deadId
	void CleanupOnDeath(EntityId deadId,
		const std::vector<HostileRefManager*>& affectedManagers);
}
