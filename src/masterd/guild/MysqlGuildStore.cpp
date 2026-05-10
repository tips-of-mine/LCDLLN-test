// Wave 5 Persistence (Phase 5.21b) - Implementation MysqlGuildStore.

#include "src/masterd/guild/MysqlGuildStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace engine::server::guilds_db
{
	namespace
	{
		/// Echappe une chaine pour MySQL via mysql_real_escape_string.
		/// Retourne vide si mysql null. Wrapper minimal aligne sur le
		/// pattern utilise dans Wave 5 phase 1 (MysqlAuctionStore).
		std::string EscapeMysql(MYSQL* mysql, std::string_view v)
		{
			if (!mysql) return {};
			std::vector<char> buf(v.size() * 2 + 1);
			const auto w = mysql_real_escape_string(mysql, buf.data(), v.data(),
				static_cast<unsigned long>(v.size()));
			return std::string(buf.data(), w);
		}
	}

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
		if (!mysql) return out;

		// Etape 1 : SELECT guildes. Index dans out / guildIndex pour
		// pouvoir attacher rapidement les membres + bank lors des queries
		// suivantes sans repasser par une boucle lineaire.
		std::unordered_map<uint32_t, size_t> guildIndex;
		{
			const char* sql =
				"SELECT guild_id, name, motd, leader_account_id, "
				"created_at_unix_ms FROM guilds_master ORDER BY guild_id ASC";
			MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
			if (!res)
			{
				LOG_WARN(Net, "[MysqlGuildStore] LoadAll guilds_master query failed");
				return out;
			}
			while (MYSQL_ROW row = mysql_fetch_row(res))
			{
				GuildMasterRow g;
				if (row[0]) g.guildId          = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
				if (row[1]) g.name             = row[1];
				if (row[2]) g.motd             = row[2];
				if (row[3]) g.leaderAccountId  = std::strtoull(row[3], nullptr, 10);
				if (row[4]) g.createdAtUnixMs  = std::strtoull(row[4], nullptr, 10);
				guildIndex[g.guildId] = out.size();
				out.push_back(std::move(g));
			}
			engine::server::db::DbFreeResult(res);
		}

		if (out.empty())
		{
			LOG_INFO(Net, "[MysqlGuildStore] LoadAll : 0 guilds (DB empty, caller will fallback)");
			return out;
		}

		// Etape 2 : SELECT membres. Une seule query pour toutes les
		// guildes (V1 N reste petit).
		{
			const char* sql =
				"SELECT guild_id, account_id, rank_id, joined_at_unix_ms "
				"FROM guild_members ORDER BY guild_id ASC, rank_id ASC, account_id ASC";
			MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
			if (!res)
			{
				LOG_WARN(Net, "[MysqlGuildStore] LoadAll guild_members query failed");
				// On retourne quand meme les guildes meme sans membres.
				return out;
			}
			while (MYSQL_ROW row = mysql_fetch_row(res))
			{
				GuildMemberRow m;
				if (row[0]) m.guildId         = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
				if (row[1]) m.accountId       = std::strtoull(row[1], nullptr, 10);
				if (row[2]) m.rankId          = static_cast<uint8_t>(std::strtoul(row[2], nullptr, 10));
				if (row[3]) m.joinedAtUnixMs  = std::strtoull(row[3], nullptr, 10);
				auto it = guildIndex.find(m.guildId);
				if (it != guildIndex.end())
					out[it->second].members.push_back(std::move(m));
			}
			engine::server::db::DbFreeResult(res);
		}

		// Etape 3 : SELECT bank tab 0.
		{
			const char* sql =
				"SELECT guild_id, tab_index, slot_index, item_template_id, "
				"item_name, count FROM guild_bank WHERE tab_index = 0 "
				"ORDER BY guild_id ASC, slot_index ASC";
			MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
			if (!res)
			{
				LOG_WARN(Net, "[MysqlGuildStore] LoadAll guild_bank query failed");
				return out;
			}
			while (MYSQL_ROW row = mysql_fetch_row(res))
			{
				GuildBankItemRow b;
				if (row[0]) b.guildId         = static_cast<uint32_t>(std::strtoul(row[0], nullptr, 10));
				if (row[1]) b.tabIndex        = static_cast<uint8_t>(std::strtoul(row[1], nullptr, 10));
				if (row[2]) b.slotIndex       = static_cast<uint32_t>(std::strtoul(row[2], nullptr, 10));
				if (row[3]) b.itemTemplateId  = static_cast<uint32_t>(std::strtoul(row[3], nullptr, 10));
				if (row[4]) b.itemName        = row[4];
				if (row[5]) b.count           = static_cast<uint32_t>(std::strtoul(row[5], nullptr, 10));
				auto it = guildIndex.find(b.guildId);
				if (it != guildIndex.end())
					out[it->second].bank0.push_back(std::move(b));
			}
			engine::server::db::DbFreeResult(res);
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
		if (!mysql) return 0u;

		const std::string nameEsc = EscapeMysql(mysql, name);
		const std::string motdEsc = EscapeMysql(mysql, motd);

		char sql[1024];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO guilds_master (name, motd, leader_account_id, created_at_unix_ms) "
			"VALUES ('%s', '%s', %llu, %llu)",
			nameEsc.c_str(),
			motdEsc.c_str(),
			static_cast<unsigned long long>(leaderAccountId),
			static_cast<unsigned long long>(createdAtUnixMs));

		if (!engine::server::db::DbExecute(mysql, sql))
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
		if (!mysql) return false;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO guild_members (guild_id, account_id, rank_id, joined_at_unix_ms) "
			"VALUES (%u, %llu, %u, %llu)",
			guildId,
			static_cast<unsigned long long>(accountId),
			static_cast<unsigned>(rankId),
			static_cast<unsigned long long>(joinedAtUnixMs));

		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (!ok)
			LOG_WARN(Net, "[MysqlGuildStore] InsertMember failed guildId={} accountId={}", guildId, accountId);
		return ok;
	}

	bool MysqlGuildStore::UpdateMotd(uint32_t guildId, std::string_view newMotd)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		const std::string motdEsc = EscapeMysql(mysql, newMotd);

		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"UPDATE guilds_master SET motd = '%s' WHERE guild_id = %u",
			motdEsc.c_str(), guildId);

		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (!ok)
			LOG_WARN(Net, "[MysqlGuildStore] UpdateMotd failed guildId={}", guildId);
		return ok;
	}
}
