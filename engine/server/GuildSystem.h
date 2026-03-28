#pragma once
// M32.3 — Server-side guild system: creation, roster, ranks, permissions, guild chat routing.
// Depends on M13.1 (server core) and M14.4 (character persistence).
// On platforms without MySQL (WIN32 game shard) the system operates in no-DB mode:
// all data is in-memory only; DB operations are skipped.

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct MYSQL;

namespace engine::server
{
	/// Permission bits for guild ranks (M32.3).
	/// These values are stored in guild_ranks.permissions_bitfield.
	enum class GuildPermission : uint32_t
	{
		None         = 0,
		Invite       = 1 << 0, ///< Can invite new members to the guild.
		Kick         = 1 << 1, ///< Can kick members below their own rank.
		Promote      = 1 << 2, ///< Can promote members below their own rank.
		WithdrawBank = 1 << 3, ///< Can withdraw gold from the guild bank.
		EditMotd     = 1 << 4, ///< Can edit the guild message of the day.
	};

	/// Built-in rank ids for the four default ranks (rank_id column, lower = higher authority).
	enum class DefaultGuildRank : uint8_t
	{
		GuildMaster = 0, ///< Full permissions; may disband.
		Officer     = 1, ///< Invite, kick, promote, edit MOTD.
		Member      = 2, ///< No management permissions.
		Recruit     = 3, ///< Trial rank; no management permissions.
	};

	/// One rank definition stored in guild_ranks (M32.3).
	struct GuildRankRecord
	{
		uint8_t     rankId              = 0;
		std::string rankName;
		uint32_t    permissionsBitfield = 0; ///< OR of GuildPermission values.
	};

	/// One member entry loaded from guild_members (M32.3).
	struct GuildMemberRecord
	{
		uint64_t    playerId   = 0;
		std::string playerName;
		uint8_t     rankId     = static_cast<uint8_t>(DefaultGuildRank::Recruit);
	};

	/// In-memory representation of one live guild (M32.3).
	struct GuildRecord
	{
		uint64_t                       guildId        = 0;
		std::string                    name;
		std::string                    motd;
		uint64_t                       masterPlayerId = 0;
		std::vector<GuildMemberRecord> members;
		std::vector<GuildRankRecord>   ranks;
	};

	/// Server-side guild manager (M32.3).
	///
	/// Handles /guild create <name>, /ginvite <name>, /gkick, /gpromote,
	/// MOTD updates, guild chat routing to online members, and DB persistence.
	///
	/// All operations are single-threaded (server authoritative tick).
	class GuildSystem final
	{
	public:
		GuildSystem() = default;

		/// Non-copyable, non-movable.
		GuildSystem(const GuildSystem&)            = delete;
		GuildSystem& operator=(const GuildSystem&) = delete;

		/// Initialize the guild system.
		/// \p mysql may be nullptr for no-DB mode (e.g. WIN32 game shard).
		/// Emits LOG_INFO on success; LOG_WARN when no DB is available.
		bool Init(MYSQL* mysql);

		/// Shut down the guild system and release all in-memory state.
		/// Emits LOG_INFO on completion.
		void Shutdown();

		bool IsInitialized() const { return m_initialized; }

		// ------------------------------------------------------------------
		// Guild lifecycle
		// ------------------------------------------------------------------

		/// Create a new guild.
		/// Validates the guild name (length 3-20, alphanumeric/space/dash/underscore),
		/// checks uniqueness, inserts DB rows when available.
		/// The caller is responsible for deducting the creation cost (1000 gold) before
		/// calling this function.
		/// Returns the new guild id (>0) on success; 0 on failure.
		uint64_t CreateGuild(uint64_t         founderId,
		                     std::string_view founderName,
		                     std::string_view guildName,
		                     MYSQL*           mysql);

		/// Disband a guild (Guild Master only).
		/// Removes all DB rows and in-memory state for the guild.
		/// Returns true on success; false when requester is not the GM.
		bool DisbandGuild(uint64_t guildId,
		                  uint64_t requesterId,
		                  MYSQL*   mysql);

		// ------------------------------------------------------------------
		// Member management
		// ------------------------------------------------------------------

		/// Add \p targetId to \p guildId after invite acceptance.
		/// Validates Invite permission for \p inviterId and guild capacity.
		/// New members receive the Recruit rank.
		/// Returns true on success.
		bool AddMember(uint64_t         guildId,
		               uint64_t         inviterId,
		               uint64_t         targetId,
		               std::string_view targetName,
		               MYSQL*           mysql);

		/// Kick \p targetId from \p guildId.
		/// Validates Kick permission for \p kickerId; kicker must outrank target.
		/// Guild Master cannot be kicked.
		/// Returns true on success.
		bool KickMember(uint64_t guildId,
		                uint64_t kickerId,
		                uint64_t targetId,
		                MYSQL*   mysql);

		/// Promote \p targetId to the next higher rank within \p guildId.
		/// Validates Promote permission for \p promoterId; promoter must outrank target.
		/// Cannot promote beyond Officer via this path (only GM can assign GM rank).
		/// Returns true on success.
		bool PromoteMember(uint64_t guildId,
		                   uint64_t promoterId,
		                   uint64_t targetId,
		                   MYSQL*   mysql);

		// ------------------------------------------------------------------
		// MOTD
		// ------------------------------------------------------------------

		/// Set the guild message of the day.
		/// Validates EditMotd permission for \p playerId.
		/// Returns true on success.
		bool SetMotd(uint64_t         guildId,
		             uint64_t         playerId,
		             std::string_view motd,
		             MYSQL*           mysql);

		// ------------------------------------------------------------------
		// Online presence (for guild chat routing)
		// ------------------------------------------------------------------

		/// Mark \p playerId as online and associate them with \p guildId (call on login).
		void SetOnline(uint64_t playerId, uint64_t guildId);

		/// Mark \p playerId as offline (call on disconnect/logout).
		void SetOffline(uint64_t playerId);

		// ------------------------------------------------------------------
		// Queries
		// ------------------------------------------------------------------

		/// Return the guild id for \p playerId; 0 when not in any guild.
		uint64_t GetGuildIdForPlayer(uint64_t playerId) const;

		/// Return the account ids of all currently-online members of \p guildId.
		/// Used to route guild chat messages and notifications.
		std::vector<uint64_t> GetOnlineMemberIds(uint64_t guildId) const;

		/// Return a const pointer to the guild record for \p guildId; nullptr when not found.
		const GuildRecord* GetGuild(uint64_t guildId) const;

		/// Return true when \p playerId in \p guildId has the given \p perm bit set.
		bool HasPermission(uint64_t guildId, uint64_t playerId, GuildPermission perm) const;

	private:
		/// Maximum members allowed per guild (M32.3 spec: 500, configurable).
		static constexpr size_t kMaxMembersPerGuild = 500;
		/// Minimum guild name length (inclusive).
		static constexpr size_t kGuildNameMinLen    = 3;
		/// Maximum guild name length (inclusive).
		static constexpr size_t kGuildNameMaxLen    = 20;

		bool     m_initialized = false;
		uint64_t m_nextGuildId = 1; ///< Monotonic id counter for no-DB mode.

		/// In-memory guild registry: guildId → GuildRecord.
		std::unordered_map<uint64_t, GuildRecord> m_guilds;
		/// Reverse lookup: playerId → guildId (absent = not in a guild).
		std::unordered_map<uint64_t, uint64_t>    m_playerGuildMap;
		/// Set of currently-online player ids (for guild chat / notification routing).
		std::unordered_set<uint64_t>              m_onlinePlayers;

		/// Validate guild name: length and allowed characters.
		bool ValidateGuildName(std::string_view name) const;

		/// Non-const guild lookup by id; returns nullptr when not found.
		GuildRecord*       FindGuild(uint64_t guildId);
		/// Const guild lookup by id; returns nullptr when not found.
		const GuildRecord* FindGuild(uint64_t guildId) const;

		/// Find a member entry inside \p guild; returns nullptr when absent.
		GuildMemberRecord*       FindMember(GuildRecord& guild, uint64_t playerId);
		const GuildMemberRecord* FindMember(const GuildRecord& guild, uint64_t playerId) const;

		/// Find a rank record inside \p guild by \p rankId; returns nullptr when absent.
		const GuildRankRecord* FindRank(const GuildRecord& guild, uint8_t rankId) const;

		/// Build the four default rank definitions for a freshly-created guild.
		static std::vector<GuildRankRecord> MakeDefaultRanks();

#if ENGINE_HAS_MYSQL
		/// INSERT INTO guilds; returns the auto-increment id (>0) or 0 on error.
		uint64_t DbInsertGuild(std::string_view name,
		                       uint64_t         masterPlayerId,
		                       MYSQL*           mysql);

		/// DELETE guilds, guild_members, guild_ranks rows for \p guildId.
		void DbDeleteGuild(uint64_t guildId, MYSQL* mysql);

		/// INSERT INTO guild_members.
		bool DbInsertMember(uint64_t guildId, uint64_t playerId,
		                    uint8_t rankId, MYSQL* mysql);

		/// DELETE FROM guild_members WHERE guild_id=… AND player_id=…
		bool DbDeleteMember(uint64_t guildId, uint64_t playerId, MYSQL* mysql);

		/// UPDATE guild_members SET rank_id=… WHERE guild_id=… AND player_id=…
		bool DbUpdateMemberRank(uint64_t guildId, uint64_t playerId,
		                        uint8_t rankId, MYSQL* mysql);

		/// UPDATE guilds SET motd=… WHERE id=…
		bool DbUpdateMotd(uint64_t guildId, std::string_view motd, MYSQL* mysql);

		/// INSERT the four default rank rows into guild_ranks for \p guildId.
		void DbInsertDefaultRanks(uint64_t guildId, MYSQL* mysql);
#endif // ENGINE_HAS_MYSQL
	};

} // namespace engine::server
