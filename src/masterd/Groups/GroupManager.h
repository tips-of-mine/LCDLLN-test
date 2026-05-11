#pragma once
// Wave 22 — GroupManager : registre central des groupes actifs sur le master.
// Alloue les GroupId monotoniques croissants, indexe les groupes par id ET
// par playerId pour des lookups O(1) "quel groupe contient ce player ?".
//
// Pas thread-safe : a appeler depuis le thread de tick master. Si un futur
// scenario reclame thread-safety (callback async chat handler par ex), on
// ajoutera un mutex sur les 2 maps.

#include "src/masterd/Groups/Group.h"
#include "src/masterd/Groups/GroupTypes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace engine::server::groups
{
	class GroupManager
	{
	public:
		GroupManager() = default;

		/// Cree un nouveau groupe avec \p leader comme premier membre.
		/// \return GroupId alloue (monotone croissant a partir de 1).
		/// Si le leader est deja dans un autre groupe, retire-le de l'ancien
		/// d'abord (un player ne peut etre que dans 1 groupe a la fois).
		GroupId CreateGroup(PlayerId leader, GroupType type)
		{
			// Si leader deja dans un groupe, le retirer d'abord.
			RemovePlayerFromCurrentGroup(leader);

			const GroupId id = m_nextId++;
			auto group = std::make_unique<Group>(id, type, leader);
			m_byPlayer[leader] = id;
			m_groups[id] = std::move(group);
			return id;
		}

		/// Ajoute \p playerId au groupe \p groupId. Retourne true si succes.
		/// Echoue si :
		/// - groupId inconnu
		/// - player deja dans le meme groupe (idempotent)
		/// - groupe a capacite max
		bool AddMember(GroupId groupId, PlayerId playerId, GroupRole role = GroupRole::Unknown)
		{
			auto it = m_groups.find(groupId);
			if (it == m_groups.end()) return false;
			// Si player dans un autre groupe, retirer d'abord (transferer).
			RemovePlayerFromCurrentGroup(playerId);
			if (!it->second->AddMember(playerId, role))
				return false;
			m_byPlayer[playerId] = groupId;
			return true;
		}

		/// Retire \p playerId de son groupe courant (no-op si pas dans un groupe).
		/// Si le groupe devient vide apres le retrait, le supprime.
		void RemoveMember(PlayerId playerId)
		{
			auto byIt = m_byPlayer.find(playerId);
			if (byIt == m_byPlayer.end()) return;
			const GroupId gid = byIt->second;
			m_byPlayer.erase(byIt);

			auto gIt = m_groups.find(gid);
			if (gIt == m_groups.end()) return;
			gIt->second->RemoveMember(playerId);
			if (gIt->second->IsEmpty())
				m_groups.erase(gIt);
		}

		/// Dissolve un groupe : retire tous les membres + delete. Idempotent.
		void Disband(GroupId groupId)
		{
			auto it = m_groups.find(groupId);
			if (it == m_groups.end()) return;
			// Recopie pour iterer sans modifier la map sous-jacente.
			const auto memberCopy = it->second->Members();
			for (const auto& kv : memberCopy)
				m_byPlayer.erase(kv.first);
			m_groups.erase(it);
		}

		/// Lookup groupe par id. Retourne nullptr si inconnu.
		Group* Find(GroupId groupId)
		{
			auto it = m_groups.find(groupId);
			return (it != m_groups.end()) ? it->second.get() : nullptr;
		}

		const Group* Find(GroupId groupId) const
		{
			auto it = m_groups.find(groupId);
			return (it != m_groups.end()) ? it->second.get() : nullptr;
		}

		/// Lookup groupe d'un player. Retourne nullopt si pas dans un groupe.
		std::optional<GroupId> GroupOfPlayer(PlayerId playerId) const
		{
			auto it = m_byPlayer.find(playerId);
			if (it == m_byPlayer.end()) return std::nullopt;
			return it->second;
		}

		size_t GroupCount() const noexcept { return m_groups.size(); }

	private:
		/// Helper interne : si \p playerId est dans un groupe, l'en retirer.
		/// No-op sinon. Utilise par CreateGroup + AddMember pour gerer le
		/// transfert d'un groupe a un autre.
		void RemovePlayerFromCurrentGroup(PlayerId playerId)
		{
			auto it = m_byPlayer.find(playerId);
			if (it == m_byPlayer.end()) return;
			const GroupId oldId = it->second;
			m_byPlayer.erase(it);
			auto gIt = m_groups.find(oldId);
			if (gIt != m_groups.end())
			{
				gIt->second->RemoveMember(playerId);
				if (gIt->second->IsEmpty())
					m_groups.erase(gIt);
			}
		}

		GroupId m_nextId = 1;
		std::unordered_map<GroupId, std::unique_ptr<Group>> m_groups;
		std::unordered_map<PlayerId, GroupId>                m_byPlayer;
	};
}
