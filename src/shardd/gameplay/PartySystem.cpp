#include "engine/server/PartySystem.h"

#include "engine/core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace engine::server
{
	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	const char* LootModeLabel(LootMode mode)
	{
		switch (mode)
		{
		case LootMode::FreeForAll:   return "FreeForAll";
		case LootMode::RoundRobin:   return "RoundRobin";
		case LootMode::MasterLooter: return "MasterLooter";
		case LootMode::NeedGreed:    return "NeedGreed";
		}
		return "Unknown";
	}

	bool ParseLootModeToken(std::string_view token, LootMode& outMode)
	{
		if (token == "ffa"        || token == "freeforall")   { outMode = LootMode::FreeForAll;   return true; }
		if (token == "roundrobin" || token == "rr")           { outMode = LootMode::RoundRobin;   return true; }
		if (token == "master"     || token == "masterlooter") { outMode = LootMode::MasterLooter; return true; }
		if (token == "needgreed"  || token == "ng")           { outMode = LootMode::NeedGreed;    return true; }
		return false;
	}

	// -------------------------------------------------------------------------
	// Init / Shutdown
	// -------------------------------------------------------------------------

	bool PartySystem::Init()
	{
		m_parties.clear();
		m_pendingInvites.clear();
		m_clientPartyMap.clear();
		m_nextPartyId = 1;
		m_initialized = true;
		LOG_INFO(Net, "[PartySystem] Init OK");
		return true;
	}

	void PartySystem::Shutdown()
	{
		m_parties.clear();
		m_pendingInvites.clear();
		m_clientPartyMap.clear();
		m_initialized = false;
		LOG_INFO(Net, "[PartySystem] Destroyed");
	}

	// -------------------------------------------------------------------------
	// Private helpers
	// -------------------------------------------------------------------------

	size_t PartySystem::FindPartyIndex(uint32_t partyId) const
	{
		for (size_t i = 0; i < m_parties.size(); ++i)
		{
			if (m_parties[i].partyId == partyId)
				return i;
		}
		return static_cast<size_t>(-1);
	}

	size_t PartySystem::FindMemberIndex(const Party& party, uint32_t clientId) const
	{
		for (size_t i = 0; i < party.members.size(); ++i)
		{
			if (party.members[i].clientId == clientId)
				return i;
		}
		return static_cast<size_t>(-1);
	}

	void PartySystem::AddMemberToParty(Party&           party,
	                                   uint32_t         clientId,
	                                   std::string_view displayName,
	                                   EntityId         entityId)
	{
		PartyMember member{};
		member.clientId    = clientId;
		member.entityId    = entityId;
		member.displayName.assign(displayName.begin(), displayName.end());
		party.members.push_back(std::move(member));
		m_clientPartyMap[clientId] = party.partyId;
	}

	void PartySystem::HandleLeaderLeft(size_t partyIndex, size_t removedMemberIndex)
	{
		Party& party = m_parties[partyIndex];

		// Remove the member from the vector first.
		party.members.erase(party.members.begin() + static_cast<ptrdiff_t>(removedMemberIndex));

		if (party.members.empty())
		{
			LOG_INFO(Net, "[PartySystem] Party disbanded: no members left (party_id={})", party.partyId);
			DisbandParty(partyIndex);
			return;
		}

		// Promote the first remaining member.
		party.leaderId = party.members[0].clientId;
		// Keep round-robin index in bounds.
		if (party.roundRobinIndex >= party.members.size())
			party.roundRobinIndex = 0;

		LOG_INFO(Net, "[PartySystem] Leader left: promoted client_id={} (party_id={})",
		    party.leaderId, party.partyId);
	}

	void PartySystem::DisbandParty(size_t partyIndex)
	{
		const Party& party = m_parties[partyIndex];
		for (const PartyMember& m : party.members)
			m_clientPartyMap.erase(m.clientId);

		LOG_INFO(Net, "[PartySystem] Party disbanded (party_id={})", party.partyId);
		m_parties.erase(m_parties.begin() + static_cast<ptrdiff_t>(partyIndex));
	}

	// -------------------------------------------------------------------------
	// Party lifecycle
	// -------------------------------------------------------------------------

	uint32_t PartySystem::CreateParty(uint32_t         leaderId,
	                                  std::string_view leaderDisplayName,
	                                  EntityId         leaderEntityId)
	{
		if (m_clientPartyMap.count(leaderId))
		{
			LOG_WARN(Net, "[PartySystem] CreateParty ignored: client already in a party (client_id={})", leaderId);
			return 0;
		}

		Party party{};
		party.partyId  = m_nextPartyId++;
		party.leaderId = leaderId;
		AddMemberToParty(party, leaderId, leaderDisplayName, leaderEntityId);
		const uint32_t newId = party.partyId;
		m_parties.push_back(std::move(party));

		LOG_INFO(Net, "[PartySystem] Party created (party_id={}, leader_client_id={})", newId, leaderId);
		return newId;
	}

	uint32_t PartySystem::LeaveParty(uint32_t clientId)
	{
		const auto mapIt = m_clientPartyMap.find(clientId);
		if (mapIt == m_clientPartyMap.end())
		{
			LOG_WARN(Net, "[PartySystem] LeaveParty ignored: client not in any party (client_id={})", clientId);
			return 0;
		}

		const uint32_t partyId   = mapIt->second;
		const size_t   partyIdx  = FindPartyIndex(partyId);
		if (partyIdx == static_cast<size_t>(-1))
		{
			// Map is out of sync — clean up.
			m_clientPartyMap.erase(mapIt);
			LOG_ERROR(Net, "[PartySystem] LeaveParty: party_id={} not found (client_id={})", partyId, clientId);
			return 0;
		}

		Party& party = m_parties[partyIdx];
		const size_t memberIdx = FindMemberIndex(party, clientId);
		if (memberIdx == static_cast<size_t>(-1))
		{
			m_clientPartyMap.erase(mapIt);
			LOG_ERROR(Net, "[PartySystem] LeaveParty: member not found in party (client_id={}, party_id={})", clientId, partyId);
			return 0;
		}

		m_clientPartyMap.erase(mapIt);

		const bool wasLeader = (party.leaderId == clientId);
		LOG_INFO(Net, "[PartySystem] Client left party (client_id={}, party_id={}, was_leader={})",
		    clientId, partyId, wasLeader ? 1 : 0);

		if (wasLeader)
		{
			// HandleLeaderLeft erases the member and either promotes or disbands.
			HandleLeaderLeft(partyIdx, memberIdx);
		}
		else
		{
			party.members.erase(party.members.begin() + static_cast<ptrdiff_t>(memberIdx));
			if (party.roundRobinIndex >= party.members.size() && !party.members.empty())
				party.roundRobinIndex = 0;

			// If only one member remains, disband.
			if (party.members.size() == 1)
			{
				LOG_INFO(Net, "[PartySystem] Party disbanded: only one member remaining (party_id={})", partyId);
				DisbandParty(FindPartyIndex(partyId)); // re-look-up after potential earlier ops
			}
		}

		return partyId;
	}

	bool PartySystem::KickMember(uint32_t partyId,
	                             uint32_t requestingClientId,
	                             uint32_t targetClientId)
	{
		const size_t partyIdx = FindPartyIndex(partyId);
		if (partyIdx == static_cast<size_t>(-1))
		{
			LOG_WARN(Net, "[PartySystem] KickMember ignored: party_id={} not found", partyId);
			return false;
		}

		Party& party = m_parties[partyIdx];
		if (party.leaderId != requestingClientId)
		{
			LOG_WARN(Net, "[PartySystem] KickMember ignored: requester is not leader (client_id={}, party_id={})",
			    requestingClientId, partyId);
			return false;
		}

		if (targetClientId == requestingClientId)
		{
			LOG_WARN(Net, "[PartySystem] KickMember ignored: leader cannot kick themselves (client_id={})", requestingClientId);
			return false;
		}

		const size_t memberIdx = FindMemberIndex(party, targetClientId);
		if (memberIdx == static_cast<size_t>(-1))
		{
			LOG_WARN(Net, "[PartySystem] KickMember ignored: target not in party (target_client_id={}, party_id={})",
			    targetClientId, partyId);
			return false;
		}

		m_clientPartyMap.erase(targetClientId);
		party.members.erase(party.members.begin() + static_cast<ptrdiff_t>(memberIdx));
		if (party.roundRobinIndex >= party.members.size() && !party.members.empty())
			party.roundRobinIndex = 0;

		LOG_INFO(Net, "[PartySystem] Member kicked (target_client_id={}, party_id={}, by_client_id={})",
		    targetClientId, partyId, requestingClientId);

		// Disband if only the leader remains.
		if (party.members.size() == 1)
		{
			LOG_INFO(Net, "[PartySystem] Party disbanded after kick: only leader remaining (party_id={})", partyId);
			DisbandParty(FindPartyIndex(partyId));
		}

		return true;
	}

	bool PartySystem::SetLootMode(uint32_t partyId,
	                              uint32_t requestingClientId,
	                              LootMode mode)
	{
		const size_t partyIdx = FindPartyIndex(partyId);
		if (partyIdx == static_cast<size_t>(-1))
		{
			LOG_WARN(Net, "[PartySystem] SetLootMode ignored: party_id={} not found", partyId);
			return false;
		}

		Party& party = m_parties[partyIdx];
		if (party.leaderId != requestingClientId)
		{
			LOG_WARN(Net, "[PartySystem] SetLootMode ignored: requester is not leader (client_id={}, party_id={})",
			    requestingClientId, partyId);
			return false;
		}

		party.lootMode = mode;
		LOG_INFO(Net, "[PartySystem] Loot mode changed (party_id={}, mode={})", partyId, LootModeLabel(mode));
		return true;
	}

	// -------------------------------------------------------------------------
	// Invite flow
	// -------------------------------------------------------------------------

	bool PartySystem::SendInvite(uint32_t         inviterClientId,
	                             uint32_t         inviteeClientId,
	                             std::string_view inviteeName,
	                             uint32_t         currentTick)
	{
		// Invitee must not be already in a party or have a pending invite.
		if (m_clientPartyMap.count(inviteeClientId))
		{
			LOG_WARN(Net, "[PartySystem] SendInvite ignored: invitee already in a party (invitee_client_id={})", inviteeClientId);
			return false;
		}
		if (m_pendingInvites.count(inviteeClientId))
		{
			LOG_WARN(Net, "[PartySystem] SendInvite ignored: invitee already has pending invite (invitee_client_id={})", inviteeClientId);
			return false;
		}

		// Inviter's party must not be full.
		const auto mapIt = m_clientPartyMap.find(inviterClientId);
		if (mapIt != m_clientPartyMap.end())
		{
			const size_t partyIdx = FindPartyIndex(mapIt->second);
			if (partyIdx != static_cast<size_t>(-1) &&
			    m_parties[partyIdx].members.size() >= kMaxPartyMembers)
			{
				LOG_WARN(Net, "[PartySystem] SendInvite ignored: party is full (party_id={}, max={})",
				    mapIt->second, kMaxPartyMembers);
				return false;
			}
		}

		PendingPartyInvite invite{};
		invite.inviterClientId = inviterClientId;
		invite.inviteeClientId = inviteeClientId;
		invite.inviteeName.assign(inviteeName.begin(), inviteeName.end());
		invite.expiresAtTick   = currentTick + kInviteExpiryTicks;
		m_pendingInvites[inviteeClientId] = std::move(invite);

		LOG_DEBUG(Net, "[PartySystem] Invite sent (inviter_client_id={}, invitee_client_id={}, expires_tick={})",
		    inviterClientId, inviteeClientId, currentTick + kInviteExpiryTicks);
		return true;
	}

	uint32_t PartySystem::AcceptInvite(uint32_t         inviteeClientId,
	                                   std::string_view inviteeDisplayName,
	                                   EntityId         inviteeEntityId,
	                                   uint32_t         currentTick)
	{
		const auto inviteIt = m_pendingInvites.find(inviteeClientId);
		if (inviteIt == m_pendingInvites.end())
		{
			LOG_WARN(Net, "[PartySystem] AcceptInvite ignored: no pending invite (client_id={})", inviteeClientId);
			return 0;
		}

		const PendingPartyInvite invite = inviteIt->second;
		m_pendingInvites.erase(inviteIt);

		if (currentTick > invite.expiresAtTick)
		{
			LOG_WARN(Net, "[PartySystem] AcceptInvite ignored: invite expired (client_id={}, expired_tick={})",
			    inviteeClientId, invite.expiresAtTick);
			return 0;
		}

		// Get or create the inviter's party.
		uint32_t partyId = 0;
		const auto mapIt = m_clientPartyMap.find(invite.inviterClientId);
		if (mapIt != m_clientPartyMap.end())
		{
			partyId = mapIt->second;
		}
		else
		{
			// Inviter is no longer in a party (they may have left); create a new one.
			// We don't have the inviter's display/entity here — use a stub if absent.
			// The inviter's stats will be updated via UpdateMemberStats next tick.
			Party party{};
			party.partyId  = m_nextPartyId++;
			party.leaderId = invite.inviterClientId;
			PartyMember leader{};
			leader.clientId    = invite.inviterClientId;
			leader.displayName = "P" + std::to_string(invite.inviterClientId);
			party.members.push_back(std::move(leader));
			m_clientPartyMap[invite.inviterClientId] = party.partyId;
			partyId = party.partyId;
			m_parties.push_back(std::move(party));
			LOG_INFO(Net, "[PartySystem] New party created on invite-accept (party_id={}, leader_client_id={})",
			    partyId, invite.inviterClientId);
		}

		const size_t partyIdx = FindPartyIndex(partyId);
		if (partyIdx == static_cast<size_t>(-1))
		{
			LOG_ERROR(Net, "[PartySystem] AcceptInvite FAILED: party_id={} not found after lookup", partyId);
			return 0;
		}

		Party& party = m_parties[partyIdx];
		if (party.members.size() >= kMaxPartyMembers)
		{
			LOG_WARN(Net, "[PartySystem] AcceptInvite ignored: party is now full (party_id={})", partyId);
			return 0;
		}

		AddMemberToParty(party, inviteeClientId, inviteeDisplayName, inviteeEntityId);
		LOG_INFO(Net, "[PartySystem] Invite accepted (invitee_client_id={}, party_id={}, members={})",
		    inviteeClientId, partyId, party.members.size());
		return partyId;
	}

	bool PartySystem::DeclineInvite(uint32_t inviteeClientId)
	{
		const auto it = m_pendingInvites.find(inviteeClientId);
		if (it == m_pendingInvites.end())
		{
			LOG_WARN(Net, "[PartySystem] DeclineInvite: no pending invite for client_id={}", inviteeClientId);
			return false;
		}
		LOG_DEBUG(Net, "[PartySystem] Invite declined (invitee_client_id={}, inviter_client_id={})",
		    inviteeClientId, it->second.inviterClientId);
		m_pendingInvites.erase(it);
		return true;
	}

	void PartySystem::ExpireInvites(uint32_t currentTick)
	{
		for (auto it = m_pendingInvites.begin(); it != m_pendingInvites.end(); )
		{
			if (currentTick > it->second.expiresAtTick)
			{
				LOG_DEBUG(Net, "[PartySystem] Invite expired (invitee_client_id={}, inviter_client_id={})",
				    it->first, it->second.inviterClientId);
				it = m_pendingInvites.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	bool PartySystem::HasPendingInvite(uint32_t inviteeClientId) const
	{
		return m_pendingInvites.count(inviteeClientId) != 0;
	}

	const PendingPartyInvite* PartySystem::GetPendingInvite(uint32_t inviteeClientId) const
	{
		const auto it = m_pendingInvites.find(inviteeClientId);
		return (it != m_pendingInvites.end()) ? &it->second : nullptr;
	}

	// -------------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------------

	Party* PartySystem::FindPartyByMember(uint32_t clientId)
	{
		const auto mapIt = m_clientPartyMap.find(clientId);
		if (mapIt == m_clientPartyMap.end())
			return nullptr;
		const size_t idx = FindPartyIndex(mapIt->second);
		return (idx != static_cast<size_t>(-1)) ? &m_parties[idx] : nullptr;
	}

	const Party* PartySystem::FindPartyByMember(uint32_t clientId) const
	{
		const auto mapIt = m_clientPartyMap.find(clientId);
		if (mapIt == m_clientPartyMap.end())
			return nullptr;
		const size_t idx = FindPartyIndex(mapIt->second);
		return (idx != static_cast<size_t>(-1)) ? &m_parties[idx] : nullptr;
	}

	Party* PartySystem::FindPartyById(uint32_t partyId)
	{
		const size_t idx = FindPartyIndex(partyId);
		return (idx != static_cast<size_t>(-1)) ? &m_parties[idx] : nullptr;
	}

	const Party* PartySystem::FindPartyById(uint32_t partyId) const
	{
		const size_t idx = FindPartyIndex(partyId);
		return (idx != static_cast<size_t>(-1)) ? &m_parties[idx] : nullptr;
	}

	bool PartySystem::IsPartyLeader(uint32_t clientId) const
	{
		const Party* party = FindPartyByMember(clientId);
		return party != nullptr && party->leaderId == clientId;
	}

	EntityId PartySystem::GetNextLooterEntityId(uint32_t partyId)
	{
		const size_t partyIdx = FindPartyIndex(partyId);
		if (partyIdx == static_cast<size_t>(-1))
			return 0;

		Party& party = m_parties[partyIdx];
		if (party.members.empty())
			return 0;

		switch (party.lootMode)
		{
		case LootMode::RoundRobin:
		{
			if (party.roundRobinIndex >= party.members.size())
				party.roundRobinIndex = 0;
			const EntityId entityId = party.members[party.roundRobinIndex].entityId;
			party.roundRobinIndex = (party.roundRobinIndex + 1) % party.members.size();
			LOG_DEBUG(Net, "[PartySystem] Round-robin looter assigned (entity_id={}, party_id={})", entityId, partyId);
			return entityId;
		}
		case LootMode::MasterLooter:
		{
			for (const PartyMember& m : party.members)
			{
				if (m.clientId == party.leaderId)
					return m.entityId;
			}
			return 0;
		}
		case LootMode::FreeForAll:
		case LootMode::NeedGreed:
		default:
			return 0;
		}
	}

	std::vector<uint32_t> PartySystem::GetMembersInRange(
	    uint32_t partyId,
	    uint32_t referenceClientId,
	    float    refPosX,
	    float    refPosZ,
	    float    rangeMeters,
	    const std::function<bool(uint32_t, float&, float&)>& posProvider) const
	{
		std::vector<uint32_t> result;
		const Party* party = FindPartyById(partyId);
		if (!party)
			return result;

		const float rangeSquared = rangeMeters * rangeMeters;

		for (const PartyMember& m : party->members)
		{
			if (m.clientId == referenceClientId)
			{
				result.push_back(m.clientId);
				continue;
			}
			float px = 0.0f, pz = 0.0f;
			if (!posProvider(m.clientId, px, pz))
				continue;

			const float dx = px - refPosX;
			const float dz = pz - refPosZ;
			if (dx * dx + dz * dz <= rangeSquared)
				result.push_back(m.clientId);
		}
		return result;
	}

	// -------------------------------------------------------------------------
	// Stats sync
	// -------------------------------------------------------------------------

	void PartySystem::UpdateMemberStats(uint32_t partyId,
	                                    uint32_t clientId,
	                                    uint32_t currentHealth,
	                                    uint32_t maxHealth,
	                                    uint32_t currentMana,
	                                    uint32_t maxMana)
	{
		const size_t partyIdx = FindPartyIndex(partyId);
		if (partyIdx == static_cast<size_t>(-1))
			return;

		Party& party = m_parties[partyIdx];
		const size_t memberIdx = FindMemberIndex(party, clientId);
		if (memberIdx == static_cast<size_t>(-1))
			return;

		PartyMember& m  = party.members[memberIdx];
		m.currentHealth = currentHealth;
		m.maxHealth     = maxHealth;
		m.currentMana   = currentMana;
		m.maxMana       = maxMana;
	}
}
