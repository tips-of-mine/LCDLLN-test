// Wave 5 Persistence (Phase 5.21b) - Implementation MysqlGuildStore.
// N1-H : converti en prepared statements (LoadAll multi-query + Insert + Update).

#include "src/masterd/guild/MysqlGuildStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

#include <unordered_map>

namespace engine::server::guilds_db
{
	bool MysqlGuildStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	std::vector<GuildMasterRow> MysqlGuildStore::LoadAll() const
	{
		std::vector<GuildMasterRow> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		// Etape 1 : SELECT guildes. Index dans out / guildIndex pour
		// pouvoir attacher rapidement les membres + bank lors des queries
		// suivantes sans repasser par une boucle lineaire.
		std::unordered_map<uint32_t, size_t> guildIndex;
		{
			auto* stmt = cache->Acquire(mysql,
				"SELECT guild_id, name, motd, leader_account_id, "
				"created_at_unix_ms FROM guilds_master ORDER BY guild_id ASC");
			if (!stmt || !stmt->Execute())
			{
				LOG_WARN(Net, "[MysqlGuildStore] LoadAll guilds_master query failed");
				return out;
			}
			while (stmt->FetchRow())
			{
				GuildMasterRow g;
				g.guildId          = static_cast<uint32_t>(stmt->GetUInt64(0));
				g.name             = stmt->GetString(1);
				g.motd             = stmt->GetString(2);
				g.leaderAccountId  = stmt->GetUInt64(3);
				g.createdAtUnixMs  = stmt->GetUInt64(4);
				guildIndex[g.guildId] = out.size();
				out.push_back(std::move(g));
			}
		}

		if (out.empty())
		{
			LOG_INFO(Net, "[MysqlGuildStore] LoadAll : 0 guilds (DB empty, caller will fallback)");
			return out;
		}

		// Etape 2 : SELECT membres. Une seule query pour toutes les
		// guildes (V1 N reste petit).
		{
			auto* stmt = cache->Acquire(mysql,
				"SELECT guild_id, account_id, rank_id, joined_at_unix_ms "
				"FROM guild_members_v2 ORDER BY guild_id ASC, rank_id ASC, account_id ASC");
			if (!stmt || !stmt->Execute())
			{
				LOG_WARN(Net, "[MysqlGuildStore] LoadAll guild_members_v2 query failed");
				return out;
			}
			while (stmt->FetchRow())
			{
				GuildMemberRow m;
				m.guildId         = static_cast<uint32_t>(stmt->GetUInt64(0));
				m.accountId       = stmt->GetUInt64(1);
				m.rankId          = static_cast<uint8_t>(stmt->GetUInt64(2));
				m.joinedAtUnixMs  = stmt->GetUInt64(3);
				auto it = guildIndex.find(m.guildId);
				if (it != guildIndex.end())
					out[it->second].members.push_back(std::move(m));
			}
		}

		// Etape 3 : SELECT bank tab 0.
		{
			auto* stmt = cache->Acquire(mysql,
				"SELECT guild_id, tab_index, slot_index, item_template_id, "
				"item_name, count FROM guild_bank WHERE tab_index = 0 "
				"ORDER BY guild_id ASC, slot_index ASC");
			if (!stmt || !stmt->Execute())
			{
				LOG_WARN(Net, "[MysqlGuildStore] LoadAll guild_bank query failed");
				return out;
			}
			while (stmt->FetchRow())
			{
				GuildBankItemRow b;
				b.guildId         = static_cast<uint32_t>(stmt->GetUInt64(0));
				b.tabIndex        = static_cast<uint8_t>(stmt->GetUInt64(1));
				b.slotIndex       = static_cast<uint32_t>(stmt->GetUInt64(2));
				b.itemTemplateId  = static_cast<uint32_t>(stmt->GetUInt64(3));
				b.itemName        = stmt->GetString(4);
				b.count           = static_cast<uint32_t>(stmt->GetUInt64(5));
				auto it = guildIndex.find(b.guildId);
				if (it != guildIndex.end())
					out[it->second].bank0.push_back(std::move(b));
			}
		}

		LOG_INFO(Net, "[MysqlGuildStore] LoadAll loaded {} guilds from DB", out.size());
		return out;
	}

	uint32_t MysqlGuildStore::InsertGuild(std::string_view name, std::string_view motd,
		uint64_t leaderAccountId, uint64_t createdAtUnixMs)
	{
		if (!IsAvailable()) return 0u;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return 0u;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO guilds_master (name, motd, leader_account_id, created_at_unix_ms) "
			"VALUES (?, ?, ?, ?)");
		const bool ok = stmt
			&& stmt->Bind(0, name)
			&& stmt->Bind(1, motd)
			&& stmt->Bind(2, leaderAccountId)
			&& stmt->Bind(3, createdAtUnixMs)
			&& stmt->Execute();
		if (!ok)
		{
			LOG_WARN(Net, "[MysqlGuildStore] InsertGuild failed name=<{}>", name);
			return 0u;
		}
		return static_cast<uint32_t>(mysql_insert_id(mysql));
	}

	bool MysqlGuildStore::InsertMember(uint32_t guildId, uint64_t accountId,
		uint8_t rankId, uint64_t joinedAtUnixMs)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"INSERT INTO guild_members_v2 (guild_id, account_id, rank_id, joined_at_unix_ms) "
			"VALUES (?, ?, ?, ?)");
		const bool ok = stmt
			&& stmt->Bind(0, guildId)
			&& stmt->Bind(1, accountId)
			&& stmt->Bind(2, static_cast<uint32_t>(rankId))
			&& stmt->Bind(3, joinedAtUnixMs)
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlGuildStore] InsertMember failed guildId={} accountId={}", guildId, accountId);
		return ok;
	}

	bool MysqlGuildStore::UpdateMotd(uint32_t guildId, std::string_view newMotd)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		auto* stmt = cache->Acquire(mysql,
			"UPDATE guilds_master SET motd = ? WHERE guild_id = ?");
		const bool ok = stmt
			&& stmt->Bind(0, newMotd)
			&& stmt->Bind(1, guildId)
			&& stmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlGuildStore] UpdateMotd failed guildId={}", guildId);
		return ok;
	}
}
