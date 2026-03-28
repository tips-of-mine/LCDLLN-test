// M32.3 — Server-side guild system implementation.
// Handles creation, roster management, ranks, permissions and guild chat routing.
// On platforms without MySQL (WIN32 game shard) the system operates in no-DB mode:
// presence tracking and in-memory state are fully functional; DB operations are skipped.

#include "engine/server/GuildSystem.h"
#include "engine/core/Log.h"

#if ENGINE_HAS_MYSQL
#  include "engine/server/db/DbHelpers.h"
#  include <mysql.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>

namespace engine::server
{
	// =========================================================================
	// Static helpers
	// =========================================================================

	/*static*/
	std::vector<GuildRankRecord> GuildSystem::MakeDefaultRanks()
	{
		using P = GuildPermission;

		constexpr uint32_t kAllPerms =
			static_cast<uint32_t>(P::Invite)       |
			static_cast<uint32_t>(P::Kick)         |
			static_cast<uint32_t>(P::Promote)      |
			static_cast<uint32_t>(P::WithdrawBank) |
			static_cast<uint32_t>(P::EditMotd);

		constexpr uint32_t kOfficerPerms =
			static_cast<uint32_t>(P::Invite)  |
			static_cast<uint32_t>(P::Kick)    |
			static_cast<uint32_t>(P::Promote) |
			static_cast<uint32_t>(P::EditMotd);

		return {
			{ static_cast<uint8_t>(DefaultGuildRank::GuildMaster), "Guild Master", kAllPerms     },
			{ static_cast<uint8_t>(DefaultGuildRank::Officer),     "Officer",      kOfficerPerms },
			{ static_cast<uint8_t>(DefaultGuildRank::Member),      "Member",       0u            },
			{ static_cast<uint8_t>(DefaultGuildRank::Recruit),     "Recruit",      0u            },
		};
	}

	// =========================================================================
	// Init / Shutdown
	// =========================================================================

	bool GuildSystem::Init(MYSQL* mysql)
	{
		m_guilds.clear();
		m_playerGuildMap.clear();
		m_onlinePlayers.clear();
		m_nextGuildId = 1;
		m_initialized = true;

#if ENGINE_HAS_MYSQL
		if (!mysql)
			LOG_WARN(Server, "[GuildSystem] Init in no-DB mode (mysql=null)");
		else
			LOG_INFO(Server, "[GuildSystem] Init OK (DB mode)");
#else
		(void)mysql;
		LOG_INFO(Server, "[GuildSystem] Init OK (no-DB mode)");
#endif
		return true;
	}

	void GuildSystem::Shutdown()
	{
		m_guilds.clear();
		m_playerGuildMap.clear();
		m_onlinePlayers.clear();
		m_initialized = false;
		LOG_INFO(Server, "[GuildSystem] Shutdown");
	}

	// =========================================================================
	// Private helpers
	// =========================================================================

	bool GuildSystem::ValidateGuildName(std::string_view name) const
	{
		if (name.size() < kGuildNameMinLen || name.size() > kGuildNameMaxLen)
			return false;
		for (char c : name)
		{
			if (!std::isalnum(static_cast<unsigned char>(c)) &&
			    c != ' ' && c != '-' && c != '_')
				return false;
		}
		return true;
	}

	GuildRecord* GuildSystem::FindGuild(uint64_t guildId)
	{
		auto it = m_guilds.find(guildId);
		return (it != m_guilds.end()) ? &it->second : nullptr;
	}

	const GuildRecord* GuildSystem::FindGuild(uint64_t guildId) const
	{
		auto it = m_guilds.find(guildId);
		return (it != m_guilds.end()) ? &it->second : nullptr;
	}

	GuildMemberRecord* GuildSystem::FindMember(GuildRecord& guild, uint64_t playerId)
	{
		for (auto& m : guild.members)
			if (m.playerId == playerId) return &m;
		return nullptr;
	}

	const GuildMemberRecord* GuildSystem::FindMember(const GuildRecord& guild, uint64_t playerId) const
	{
		for (const auto& m : guild.members)
			if (m.playerId == playerId) return &m;
		return nullptr;
	}

	const GuildRankRecord* GuildSystem::FindRank(const GuildRecord& guild, uint8_t rankId) const
	{
		for (const auto& r : guild.ranks)
			if (r.rankId == rankId) return &r;
		return nullptr;
	}

	// =========================================================================
	// Queries
	// =========================================================================

	uint64_t GuildSystem::GetGuildIdForPlayer(uint64_t playerId) const
	{
		auto it = m_playerGuildMap.find(playerId);
		return (it != m_playerGuildMap.end()) ? it->second : 0u;
	}

	const GuildRecord* GuildSystem::GetGuild(uint64_t guildId) const
	{
		return FindGuild(guildId);
	}

	bool GuildSystem::HasPermission(uint64_t guildId, uint64_t playerId, GuildPermission perm) const
	{
		const GuildRecord* guild = FindGuild(guildId);
		if (!guild)
			return false;
		const GuildMemberRecord* member = FindMember(*guild, playerId);
		if (!member)
			return false;
		// Guild Master implicitly holds all permissions.
		if (member->rankId == static_cast<uint8_t>(DefaultGuildRank::GuildMaster))
			return true;
		const GuildRankRecord* rank = FindRank(*guild, member->rankId);
		if (!rank)
			return false;
		return (rank->permissionsBitfield & static_cast<uint32_t>(perm)) != 0u;
	}

	std::vector<uint64_t> GuildSystem::GetOnlineMemberIds(uint64_t guildId) const
	{
		const GuildRecord* guild = FindGuild(guildId);
		if (!guild)
			return {};
		std::vector<uint64_t> result;
		result.reserve(guild->members.size());
		for (const auto& m : guild->members)
		{
			if (m_onlinePlayers.count(m.playerId))
				result.push_back(m.playerId);
		}
		return result;
	}

	// =========================================================================
	// Online presence
	// =========================================================================

	void GuildSystem::SetOnline(uint64_t playerId, uint64_t guildId)
	{
		m_onlinePlayers.insert(playerId);
		if (guildId != 0u)
			m_playerGuildMap[playerId] = guildId;
		LOG_DEBUG(Server, "[GuildSystem] Player {} online (guild={})", playerId, guildId);
	}

	void GuildSystem::SetOffline(uint64_t playerId)
	{
		m_onlinePlayers.erase(playerId);
		LOG_DEBUG(Server, "[GuildSystem] Player {} offline", playerId);
	}

	// =========================================================================
	// Guild lifecycle — CreateGuild
	// =========================================================================

	uint64_t GuildSystem::CreateGuild(uint64_t         founderId,
	                                  std::string_view founderName,
	                                  std::string_view guildName,
	                                  MYSQL*           mysql)
	{
		if (!m_initialized)
		{
			LOG_ERROR(Server, "[GuildSystem] CreateGuild called before Init");
			return 0u;
		}

		if (!ValidateGuildName(guildName))
		{
			LOG_WARN(Server, "[GuildSystem] CreateGuild rejected: invalid name '{}'", guildName);
			return 0u;
		}

		if (m_playerGuildMap.count(founderId))
		{
			LOG_WARN(Server, "[GuildSystem] CreateGuild rejected: player {} already in a guild", founderId);
			return 0u;
		}

		// In-memory uniqueness check (DB also enforces via UNIQUE KEY).
		for (const auto& [id, g] : m_guilds)
		{
			if (g.name == guildName)
			{
				LOG_WARN(Server, "[GuildSystem] CreateGuild rejected: name '{}' already taken", guildName);
				return 0u;
			}
		}

		uint64_t newId = 0u;

#if ENGINE_HAS_MYSQL
		if (mysql)
		{
			newId = DbInsertGuild(guildName, founderId, mysql);
			if (newId == 0u)
			{
				LOG_ERROR(Server, "[GuildSystem] CreateGuild DB insert failed for '{}'", guildName);
				return 0u;
			}
			DbInsertDefaultRanks(newId, mysql);
			DbInsertMember(newId, founderId,
			               static_cast<uint8_t>(DefaultGuildRank::GuildMaster), mysql);
		}
		else
		{
			newId = m_nextGuildId++;
		}
#else
		(void)mysql;
		newId = m_nextGuildId++;
#endif

		GuildRecord guild;
		guild.guildId        = newId;
		guild.name           = std::string(guildName);
		guild.motd           = "";
		guild.masterPlayerId = founderId;
		guild.ranks          = MakeDefaultRanks();
		guild.members.push_back({
			founderId,
			std::string(founderName),
			static_cast<uint8_t>(DefaultGuildRank::GuildMaster)
		});

		m_playerGuildMap[founderId] = newId;
		m_guilds.emplace(newId, std::move(guild));

		LOG_INFO(Server, "[GuildSystem] Guild '{}' created (id={}, founder={}/{})",
		         guildName, newId, founderId, founderName);
		return newId;
	}

	// =========================================================================
	// Guild lifecycle — DisbandGuild
	// =========================================================================

	bool GuildSystem::DisbandGuild(uint64_t guildId, uint64_t requesterId, MYSQL* mysql)
	{
		GuildRecord* guild = FindGuild(guildId);
		if (!guild)
		{
			LOG_WARN(Server, "[GuildSystem] DisbandGuild: guild {} not found", guildId);
			return false;
		}

		const GuildMemberRecord* requester = FindMember(*guild, requesterId);
		if (!requester ||
		    requester->rankId != static_cast<uint8_t>(DefaultGuildRank::GuildMaster))
		{
			LOG_WARN(Server, "[GuildSystem] DisbandGuild: player {} lacks GM rank in guild {}",
			         requesterId, guildId);
			return false;
		}

#if ENGINE_HAS_MYSQL
		if (mysql)
			DbDeleteGuild(guildId, mysql);
#else
		(void)mysql;
#endif

		for (const auto& member : guild->members)
			m_playerGuildMap.erase(member.playerId);

		m_guilds.erase(guildId);
		LOG_INFO(Server, "[GuildSystem] Guild {} disbanded by player {}", guildId, requesterId);
		return true;
	}

	// =========================================================================
	// Member management — AddMember
	// =========================================================================

	bool GuildSystem::AddMember(uint64_t         guildId,
	                            uint64_t         inviterId,
	                            uint64_t         targetId,
	                            std::string_view targetName,
	                            MYSQL*           mysql)
	{
		GuildRecord* guild = FindGuild(guildId);
		if (!guild)
		{
			LOG_WARN(Server, "[GuildSystem] AddMember: guild {} not found", guildId);
			return false;
		}

		if (!HasPermission(guildId, inviterId, GuildPermission::Invite))
		{
			LOG_WARN(Server, "[GuildSystem] AddMember: player {} lacks Invite permission in guild {}",
			         inviterId, guildId);
			return false;
		}

		if (guild->members.size() >= kMaxMembersPerGuild)
		{
			LOG_WARN(Server, "[GuildSystem] AddMember: guild {} at max capacity ({})",
			         guildId, kMaxMembersPerGuild);
			return false;
		}

		if (m_playerGuildMap.count(targetId))
		{
			LOG_WARN(Server, "[GuildSystem] AddMember: player {} already in a guild", targetId);
			return false;
		}

		const uint8_t recruitRank = static_cast<uint8_t>(DefaultGuildRank::Recruit);

#if ENGINE_HAS_MYSQL
		if (mysql)
		{
			if (!DbInsertMember(guildId, targetId, recruitRank, mysql))
				return false;
		}
#else
		(void)mysql;
#endif

		guild->members.push_back({ targetId, std::string(targetName), recruitRank });
		m_playerGuildMap[targetId] = guildId;

		LOG_INFO(Server, "[GuildSystem] Player {}/{} joined guild {} (invited by {})",
		         targetId, targetName, guildId, inviterId);
		return true;
	}

	// =========================================================================
	// Member management — KickMember
	// =========================================================================

	bool GuildSystem::KickMember(uint64_t guildId,
	                             uint64_t kickerId,
	                             uint64_t targetId,
	                             MYSQL*   mysql)
	{
		GuildRecord* guild = FindGuild(guildId);
		if (!guild)
		{
			LOG_WARN(Server, "[GuildSystem] KickMember: guild {} not found", guildId);
			return false;
		}

		if (!HasPermission(guildId, kickerId, GuildPermission::Kick))
		{
			LOG_WARN(Server, "[GuildSystem] KickMember: player {} lacks Kick permission in guild {}",
			         kickerId, guildId);
			return false;
		}

		const GuildMemberRecord* kicker = FindMember(*guild, kickerId);
		const GuildMemberRecord* target = FindMember(*guild, targetId);

		if (!kicker || !target)
		{
			LOG_WARN(Server, "[GuildSystem] KickMember: member not found in guild {}", guildId);
			return false;
		}

		// Cannot kick the Guild Master.
		if (target->rankId == static_cast<uint8_t>(DefaultGuildRank::GuildMaster))
		{
			LOG_WARN(Server, "[GuildSystem] KickMember: cannot kick Guild Master (player {})", targetId);
			return false;
		}

		// Kicker must outrank target (lower rankId = higher authority).
		if (kicker->rankId != static_cast<uint8_t>(DefaultGuildRank::GuildMaster) &&
		    target->rankId <= kicker->rankId)
		{
			LOG_WARN(Server, "[GuildSystem] KickMember: player {} cannot kick {} (rank check)",
			         kickerId, targetId);
			return false;
		}

#if ENGINE_HAS_MYSQL
		if (mysql)
			DbDeleteMember(guildId, targetId, mysql);
#else
		(void)mysql;
#endif

		auto& members = guild->members;
		members.erase(
			std::remove_if(members.begin(), members.end(),
			               [targetId](const GuildMemberRecord& m) { return m.playerId == targetId; }),
			members.end());

		m_playerGuildMap.erase(targetId);

		LOG_INFO(Server, "[GuildSystem] Player {} kicked from guild {} by {}",
		         targetId, guildId, kickerId);
		return true;
	}

	// =========================================================================
	// Member management — PromoteMember
	// =========================================================================

	bool GuildSystem::PromoteMember(uint64_t guildId,
	                                uint64_t promoterId,
	                                uint64_t targetId,
	                                MYSQL*   mysql)
	{
		GuildRecord* guild = FindGuild(guildId);
		if (!guild)
		{
			LOG_WARN(Server, "[GuildSystem] PromoteMember: guild {} not found", guildId);
			return false;
		}

		if (!HasPermission(guildId, promoterId, GuildPermission::Promote))
		{
			LOG_WARN(Server,
			         "[GuildSystem] PromoteMember: player {} lacks Promote permission in guild {}",
			         promoterId, guildId);
			return false;
		}

		GuildMemberRecord* target = FindMember(*guild, targetId);
		if (!target)
		{
			LOG_WARN(Server, "[GuildSystem] PromoteMember: player {} not in guild {}", targetId, guildId);
			return false;
		}

		// Already Guild Master; cannot promote further.
		if (target->rankId == static_cast<uint8_t>(DefaultGuildRank::GuildMaster))
		{
			LOG_WARN(Server, "[GuildSystem] PromoteMember: player {} is already Guild Master", targetId);
			return false;
		}

		// Cannot promote to Guild Master via this path (reserved for explicit GM transfer).
		if (target->rankId == static_cast<uint8_t>(DefaultGuildRank::Officer) &&
		    promoterId != guild->masterPlayerId)
		{
			LOG_WARN(Server,
			         "[GuildSystem] PromoteMember: only the Guild Master can promote to GM rank (player {})",
			         targetId);
			return false;
		}

		const uint8_t newRank = static_cast<uint8_t>(target->rankId - 1u);

#if ENGINE_HAS_MYSQL
		if (mysql)
			DbUpdateMemberRank(guildId, targetId, newRank, mysql);
#else
		(void)mysql;
#endif

		const uint8_t oldRank = target->rankId;
		target->rankId = newRank;

		LOG_INFO(Server, "[GuildSystem] Player {} promoted from rank {} to {} in guild {} by {}",
		         targetId, oldRank, newRank, guildId, promoterId);
		return true;
	}

	// =========================================================================
	// MOTD
	// =========================================================================

	bool GuildSystem::SetMotd(uint64_t         guildId,
	                          uint64_t         playerId,
	                          std::string_view motd,
	                          MYSQL*           mysql)
	{
		GuildRecord* guild = FindGuild(guildId);
		if (!guild)
		{
			LOG_WARN(Server, "[GuildSystem] SetMotd: guild {} not found", guildId);
			return false;
		}

		if (!HasPermission(guildId, playerId, GuildPermission::EditMotd))
		{
			LOG_WARN(Server, "[GuildSystem] SetMotd: player {} lacks EditMotd permission in guild {}",
			         playerId, guildId);
			return false;
		}

#if ENGINE_HAS_MYSQL
		if (mysql)
		{
			if (!DbUpdateMotd(guildId, motd, mysql))
				return false;
		}
#else
		(void)mysql;
#endif

		guild->motd = std::string(motd);
		LOG_INFO(Server, "[GuildSystem] Guild {} MOTD updated by player {}", guildId, playerId);
		return true;
	}

	// =========================================================================
	// DB helpers (UNIX + ENGINE_HAS_MYSQL only)
	// =========================================================================

#if ENGINE_HAS_MYSQL

	uint64_t GuildSystem::DbInsertGuild(std::string_view name,
	                                    uint64_t         masterPlayerId,
	                                    MYSQL*           mysql)
	{
		std::string escaped(name.size() * 2u + 1u, '\0');
		unsigned long written = mysql_real_escape_string(
			mysql, escaped.data(), name.data(),
			static_cast<unsigned long>(name.size()));
		escaped.resize(static_cast<size_t>(written));

		std::string sql =
			"INSERT INTO guilds (name, master_player_id, motd) VALUES ('"
			+ escaped + "', "
			+ std::to_string(masterPlayerId)
			+ ", '')";

		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Server, "[GuildSystem] DbInsertGuild query failed: {}", mysql_error(mysql));
			return 0u;
		}
		return static_cast<uint64_t>(mysql_insert_id(mysql));
	}

	void GuildSystem::DbDeleteGuild(uint64_t guildId, MYSQL* mysql)
	{
		std::string sql;

		sql = "DELETE FROM guild_ranks WHERE guild_id = " + std::to_string(guildId);
		if (mysql_query(mysql, sql.c_str()) != 0)
			LOG_WARN(Server, "[GuildSystem] DbDeleteGuild ranks failed: {}", mysql_error(mysql));

		sql = "DELETE FROM guild_members WHERE guild_id = " + std::to_string(guildId);
		if (mysql_query(mysql, sql.c_str()) != 0)
			LOG_WARN(Server, "[GuildSystem] DbDeleteGuild members failed: {}", mysql_error(mysql));

		sql = "DELETE FROM guilds WHERE id = " + std::to_string(guildId);
		if (mysql_query(mysql, sql.c_str()) != 0)
			LOG_ERROR(Server, "[GuildSystem] DbDeleteGuild guilds row failed: {}", mysql_error(mysql));
	}

	bool GuildSystem::DbInsertMember(uint64_t guildId, uint64_t playerId,
	                                 uint8_t rankId, MYSQL* mysql)
	{
		std::string sql =
			"INSERT INTO guild_members (guild_id, player_id, rank_id) VALUES ("
			+ std::to_string(guildId) + ", "
			+ std::to_string(playerId) + ", "
			+ std::to_string(static_cast<unsigned>(rankId)) + ")";

		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Server, "[GuildSystem] DbInsertMember failed: {}", mysql_error(mysql));
			return false;
		}
		return true;
	}

	bool GuildSystem::DbDeleteMember(uint64_t guildId, uint64_t playerId, MYSQL* mysql)
	{
		std::string sql =
			"DELETE FROM guild_members WHERE guild_id = "
			+ std::to_string(guildId)
			+ " AND player_id = "
			+ std::to_string(playerId);

		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Server, "[GuildSystem] DbDeleteMember failed: {}", mysql_error(mysql));
			return false;
		}
		return true;
	}

	bool GuildSystem::DbUpdateMemberRank(uint64_t guildId, uint64_t playerId,
	                                     uint8_t rankId, MYSQL* mysql)
	{
		std::string sql =
			"UPDATE guild_members SET rank_id = "
			+ std::to_string(static_cast<unsigned>(rankId))
			+ " WHERE guild_id = " + std::to_string(guildId)
			+ " AND player_id = " + std::to_string(playerId);

		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Server, "[GuildSystem] DbUpdateMemberRank failed: {}", mysql_error(mysql));
			return false;
		}
		return true;
	}

	bool GuildSystem::DbUpdateMotd(uint64_t guildId, std::string_view motd, MYSQL* mysql)
	{
		std::string escaped(motd.size() * 2u + 1u, '\0');
		unsigned long written = mysql_real_escape_string(
			mysql, escaped.data(), motd.data(),
			static_cast<unsigned long>(motd.size()));
		escaped.resize(static_cast<size_t>(written));

		std::string sql =
			"UPDATE guilds SET motd = '"
			+ escaped
			+ "' WHERE id = " + std::to_string(guildId);

		if (mysql_query(mysql, sql.c_str()) != 0)
		{
			LOG_ERROR(Server, "[GuildSystem] DbUpdateMotd failed: {}", mysql_error(mysql));
			return false;
		}
		return true;
	}

	void GuildSystem::DbInsertDefaultRanks(uint64_t guildId, MYSQL* mysql)
	{
		using P = GuildPermission;

		constexpr uint32_t kAllPerms =
			static_cast<uint32_t>(P::Invite)       |
			static_cast<uint32_t>(P::Kick)         |
			static_cast<uint32_t>(P::Promote)      |
			static_cast<uint32_t>(P::WithdrawBank) |
			static_cast<uint32_t>(P::EditMotd);

		constexpr uint32_t kOfficerPerms =
			static_cast<uint32_t>(P::Invite)  |
			static_cast<uint32_t>(P::Kick)    |
			static_cast<uint32_t>(P::Promote) |
			static_cast<uint32_t>(P::EditMotd);

		struct RankDef { uint8_t id; const char* name; uint32_t perms; };
		constexpr RankDef kRanks[] = {
			{ 0u, "Guild Master", kAllPerms     },
			{ 1u, "Officer",      kOfficerPerms },
			{ 2u, "Member",       0u            },
			{ 3u, "Recruit",      0u            },
		};

		for (const auto& r : kRanks)
		{
			const size_t nameLen = std::strlen(r.name);
			std::string escaped(nameLen * 2u + 1u, '\0');
			unsigned long written = mysql_real_escape_string(
				mysql, escaped.data(), r.name,
				static_cast<unsigned long>(nameLen));
			escaped.resize(static_cast<size_t>(written));

			std::string sql =
				"INSERT INTO guild_ranks (guild_id, rank_id, rank_name, permissions_bitfield) VALUES ("
				+ std::to_string(guildId) + ", "
				+ std::to_string(static_cast<unsigned>(r.id)) + ", '"
				+ escaped + "', "
				+ std::to_string(r.perms) + ")";

			if (mysql_query(mysql, sql.c_str()) != 0)
			{
				LOG_WARN(Server, "[GuildSystem] DbInsertDefaultRanks rank {} failed: {}",
				         r.id, mysql_error(mysql));
			}
		}
	}

#endif // ENGINE_HAS_MYSQL

} // namespace engine::server
