#pragma once
// M32.2 — Server-side party system: invite, accept/decline, kick, loot modes, XP sharing.

#include "engine/server/ReplicationTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::server
{
	/// Authoritative loot distribution mode for a party (M32.2).
	enum class LootMode : uint8_t
	{
		/// Any member can pick up dropped items first (default).
		FreeForAll   = 0,
		/// Loot bags are assigned in rotation to party members.
		RoundRobin   = 1,
		/// The party leader receives all loot bags to distribute manually.
		MasterLooter = 2,
		/// All members in range auto-roll need/greed; highest roll wins.
		NeedGreed    = 3
	};

	/// Human-readable label for a LootMode value (used in logs and chat notices).
	const char* LootModeLabel(LootMode mode);

	/// Parse a loot-mode token ("ffa", "roundrobin", "master", "needgreed").
	/// Returns true and sets \p outMode on success; false on unknown token.
	bool ParseLootModeToken(std::string_view token, LootMode& outMode);

	/// One member entry stored inside a live Party (M32.2).
	struct PartyMember
	{
		uint32_t    clientId      = 0;
		EntityId    entityId      = 0;
		std::string displayName;
		uint32_t    currentHealth = 0;
		uint32_t    maxHealth     = 0;
		uint32_t    currentMana   = 0;
		uint32_t    maxMana       = 0;
	};

	/// One live party tracked by the authoritative server (M32.2).
	struct Party
	{
		uint32_t partyId          = 0;
		uint32_t leaderId         = 0; ///< clientId of the current party leader.
		LootMode lootMode         = LootMode::FreeForAll;
		uint32_t roundRobinIndex  = 0; ///< Next member index for round-robin loot assignment.
		std::vector<PartyMember> members;
	};

	/// One pending party invite waiting for target acceptance (M32.2).
	struct PendingPartyInvite
	{
		uint32_t    inviterClientId = 0;
		uint32_t    inviteeClientId = 0;
		std::string inviteeName;
		uint32_t    expiresAtTick   = 0;
	};

	/// Server-side party manager (M32.2).
	///
	/// Handles formation (invite/accept/decline/kick), leader promotion on
	/// leader leave, loot-mode changes, and XP-sharing range queries.
	/// All operations are single-threaded (server authoritative tick).
	class PartySystem final
	{
	public:
		PartySystem() = default;

		PartySystem(const PartySystem&)            = delete;
		PartySystem& operator=(const PartySystem&) = delete;

		/// Initialize the party system.
		/// Emits LOG_INFO on success.
		bool Init();

		/// Shut down the party system and release all runtime state.
		/// Emits LOG_INFO on completion.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ------------------------------------------------------------------
		// Party lifecycle
		// ------------------------------------------------------------------

		/// Create a new party with \p leaderId as the first member and leader.
		/// Returns the new party id (>0) on success or 0 if the client is already in a party.
		uint32_t CreateParty(uint32_t     leaderId,
		                     std::string_view leaderDisplayName,
		                     EntityId     leaderEntityId);

		/// Remove \p clientId from their current party.
		/// Promotes the next available member when the leaving client is the leader,
		/// or disbands the party when no members remain.
		/// Returns the affected partyId (0 when the client was not in any party).
		uint32_t LeaveParty(uint32_t clientId);

		/// Kick \p targetClientId from \p partyId (only the current leader may kick).
		/// Returns true on success; false when the requester is not the leader or
		/// the target is not in the party.
		bool KickMember(uint32_t partyId,
		                uint32_t requestingClientId,
		                uint32_t targetClientId);

		/// Change the loot mode for \p partyId (only the current leader may change it).
		/// Emits LOG_INFO on success.
		bool SetLootMode(uint32_t partyId,
		                 uint32_t requestingClientId,
		                 LootMode mode);

		// ------------------------------------------------------------------
		// Invite flow
		// ------------------------------------------------------------------

		/// Register a pending invite from \p inviterClientId to \p inviteeClientId.
		/// Returns false when the invitee already has a pending invite, is already
		/// in a party, or the inviter's party is at capacity.
		bool SendInvite(uint32_t         inviterClientId,
		                uint32_t         inviteeClientId,
		                std::string_view inviteeName,
		                uint32_t         currentTick);

		/// Accept the pending invite for \p inviteeClientId and add them to the inviter's party.
		/// Creates a new party when the inviter is not already in one.
		/// Returns the partyId on success or 0 on failure (expired, no invite, etc.).
		uint32_t AcceptInvite(uint32_t         inviteeClientId,
		                      std::string_view inviteeDisplayName,
		                      EntityId         inviteeEntityId,
		                      uint32_t         currentTick);

		/// Decline or cancel the pending invite for \p inviteeClientId.
		/// Returns true when an invite was found and removed.
		bool DeclineInvite(uint32_t inviteeClientId);

		/// Remove invites that have passed their expiry tick.
		void ExpireInvites(uint32_t currentTick);

		bool HasPendingInvite(uint32_t inviteeClientId) const;
		const PendingPartyInvite* GetPendingInvite(uint32_t inviteeClientId) const;

		// ------------------------------------------------------------------
		// Queries
		// ------------------------------------------------------------------

		/// Return the party that \p clientId belongs to, or nullptr.
		Party*       FindPartyByMember(uint32_t clientId);
		const Party* FindPartyByMember(uint32_t clientId) const;

		/// Return the party with the given id, or nullptr.
		Party*       FindPartyById(uint32_t partyId);
		const Party* FindPartyById(uint32_t partyId) const;

		/// Return true when \p clientId is the current leader of their party.
		bool IsPartyLeader(uint32_t clientId) const;

		/// Return the entityId of the member who should receive the next loot bag.
		///
		/// - RoundRobin : advances the rotation index and returns the selected member.
		/// - MasterLooter : returns the leader's entityId.
		/// - FreeForAll / NeedGreed : returns 0 (caller handles these modes separately).
		EntityId GetNextLooterEntityId(uint32_t partyId);

		/// Collect client ids of party members within \p rangeMeters of the reference
		/// position (XZ plane), using the supplied position-lookup callback.
		/// The reference client (\p referenceClientId) is always included.
		std::vector<uint32_t> GetMembersInRange(
		    uint32_t partyId,
		    uint32_t referenceClientId,
		    float    refPosX,
		    float    refPosZ,
		    float    rangeMeters,
		    const std::function<bool(uint32_t clientId, float& outX, float& outZ)>& posProvider) const;

		// ------------------------------------------------------------------
		// Stats sync (for party UI frame)
		// ------------------------------------------------------------------

		/// Update the cached HP / mana for one party member.
		/// Called by the server tick after every simulation step.
		void UpdateMemberStats(uint32_t partyId,
		                       uint32_t clientId,
		                       uint32_t currentHealth,
		                       uint32_t maxHealth,
		                       uint32_t currentMana,
		                       uint32_t maxMana);

	private:
		/// Maximum members allowed in one party (extensible to 10/25 for raids via config).
		static constexpr size_t kMaxPartyMembers = 5;

		/// Number of server ticks before an unanswered invite expires (~30 s at 10 Hz).
		static constexpr uint32_t kInviteExpiryTicks = 300;

		bool     m_initialized = false;
		uint32_t m_nextPartyId = 1;

		std::vector<Party>                               m_parties;
		std::unordered_map<uint32_t, PendingPartyInvite> m_pendingInvites; ///< key = inviteeClientId
		std::unordered_map<uint32_t, uint32_t>           m_clientPartyMap; ///< clientId → partyId

		/// Return the index of \p partyId in m_parties, or npos.
		size_t FindPartyIndex(uint32_t partyId) const;

		/// Return the index of \p clientId inside a party's members vector, or npos.
		size_t FindMemberIndex(const Party& party, uint32_t clientId) const;

		/// Add \p clientId to an existing party; does NOT check capacity (caller must validate).
		void AddMemberToParty(Party&           party,
		                      uint32_t         clientId,
		                      std::string_view displayName,
		                      EntityId         entityId);

		/// Promote the next available member to leader or disband if the party is empty.
		void HandleLeaderLeft(size_t partyIndex, size_t removedMemberIndex);

		/// Erase a party from m_parties and clean up m_clientPartyMap for its members.
		void DisbandParty(size_t partyIndex);
	};
}
