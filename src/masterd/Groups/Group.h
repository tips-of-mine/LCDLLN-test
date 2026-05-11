#pragma once
// Wave 22 — Group : un groupe de joueurs (party 5p ou raid 10/25/40). Master-side
// car les groupes peuvent etre cross-shard (raid future). Detient :
// - id immuable (GroupId, alloue par GroupManager)
// - type (Party / Raid) immuable apres creation
// - leader (PlayerId) — peut etre change via Promote
// - lootMethod — peut etre change par le leader
// - membres (set de PlayerId + role par membre)
//
// Pattern WoW-like adapte LCDLLN : pas de membre intrusive list (cmangos
// utilise un GroupReference doubly-linked, on simplifie en set hash-based
// car nos lookups sont domines par "is X in group ?" et "list members").

#include "src/masterd/Groups/GroupTypes.h"

#include <cstdint>
#include <unordered_map>

namespace engine::server::groups
{
	/// Donnees par membre. Role optionnel pour LFG / matchmaking.
	struct GroupMember
	{
		PlayerId  playerId = 0;
		GroupRole role     = GroupRole::Unknown;
	};

	class Group
	{
	public:
		/// \param id      identifiant alloue par GroupManager
		/// \param type    Party ou Raid (immuable apres creation)
		/// \param leader  player initial creant le groupe (auto-ajoute)
		Group(GroupId id, GroupType type, PlayerId leader)
			: m_id(id), m_type(type), m_leader(leader), m_lootMethod(LootMethod::FreeForAll)
		{
			// Le leader est auto-ajoute comme membre.
			m_members[leader] = GroupMember{leader, GroupRole::Unknown};
		}

		GroupId   Id() const noexcept { return m_id; }
		GroupType Type() const noexcept { return m_type; }
		PlayerId  Leader() const noexcept { return m_leader; }
		LootMethod CurrentLootMethod() const noexcept { return m_lootMethod; }

		/// Ajoute \p playerId au groupe avec role optionnel. Retourne false si :
		/// - le player est deja dans le groupe (idempotent : pas de re-add)
		/// - la capacite max est atteinte (5 party / 40 raid)
		bool AddMember(PlayerId playerId, GroupRole role = GroupRole::Unknown)
		{
			if (m_members.count(playerId) > 0)
				return false;  // deja present
			const uint32_t maxCap = (m_type == GroupType::Party) ? kPartyMaxMembers : kRaidMaxMembers;
			if (m_members.size() >= maxCap)
				return false;
			m_members[playerId] = GroupMember{playerId, role};
			return true;
		}

		/// Retire \p playerId du groupe. Retourne true si retire, false si absent.
		/// Si le leader part, transfere le leadership au premier membre restant
		/// (ou groupe vide si etait seul).
		bool RemoveMember(PlayerId playerId)
		{
			auto it = m_members.find(playerId);
			if (it == m_members.end()) return false;
			m_members.erase(it);
			if (m_leader == playerId && !m_members.empty())
			{
				// Transfert auto au premier restant (ordre non-deterministe :
				// le caller peut promouvoir explicitement avant un Remove si
				// la regle metier l'exige).
				m_leader = m_members.begin()->first;
			}
			return true;
		}

		bool HasMember(PlayerId playerId) const noexcept
		{
			return m_members.count(playerId) > 0;
		}

		size_t MemberCount() const noexcept { return m_members.size(); }

		const std::unordered_map<PlayerId, GroupMember>& Members() const noexcept
		{
			return m_members;
		}

		/// Promote \p playerId leader. Retourne false si non-membre.
		bool Promote(PlayerId playerId)
		{
			if (m_members.count(playerId) == 0) return false;
			m_leader = playerId;
			return true;
		}

		/// Set role pour un membre. Retourne false si non-membre.
		bool SetRole(PlayerId playerId, GroupRole role)
		{
			auto it = m_members.find(playerId);
			if (it == m_members.end()) return false;
			it->second.role = role;
			return true;
		}

		/// Change le LootMethod du groupe. Pas de gating leader-only ici (a
		/// faire dans le handler, qui valide que l'appelant == leader).
		void SetLootMethod(LootMethod m) noexcept { m_lootMethod = m; }

		bool IsEmpty() const noexcept { return m_members.empty(); }

	private:
		GroupId    m_id;
		GroupType  m_type;
		PlayerId   m_leader;
		LootMethod m_lootMethod;
		std::unordered_map<PlayerId, GroupMember> m_members;
	};
}
