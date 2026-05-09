#include "src/masterd/mail/MysqlMailStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"

#include <mysql.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace engine::server::mail
{
	namespace
	{
		std::string EscapeMysql(MYSQL* mysql, std::string_view v)
		{
			if (!mysql) return {};
			std::vector<char> buf(v.size() * 2 + 1);
			const auto w = mysql_real_escape_string(mysql, buf.data(), v.data(),
				static_cast<unsigned long>(v.size()));
			return std::string(buf.data(), w);
		}

		Mail RowToMail(MYSQL_ROW row)
		{
			Mail m;
			if (row[0]) m.mailId             = std::strtoull(row[0], nullptr, 10);
			if (row[1]) m.senderAccountId    = std::strtoull(row[1], nullptr, 10);
			if (row[2]) m.receiverAccountId  = std::strtoull(row[2], nullptr, 10);
			if (row[3]) m.subject            = row[3];
			if (row[4]) m.body               = row[4];
			if (row[5]) m.copperGold         = std::strtoull(row[5], nullptr, 10);
			if (row[6]) m.copperCod          = std::strtoull(row[6], nullptr, 10);
			if (row[7]) m.sentTsMs           = std::strtoull(row[7], nullptr, 10);
			if (row[8]) m.expiresTsMs        = std::strtoull(row[8], nullptr, 10);
			if (row[9])
			{
				const auto st = static_cast<uint32_t>(std::strtoul(row[9], nullptr, 10));
				m.state = static_cast<MailState>(std::min<uint32_t>(st, 3));
			}
			return m;
		}

		void LoadItemsForMail(MYSQL* mysql, Mail& m)
		{
			char sql[256];
			std::snprintf(sql, sizeof(sql),
				"SELECT slot, item_template_id, count FROM mail_items "
				"WHERE mail_id = %llu ORDER BY slot ASC",
				static_cast<unsigned long long>(m.mailId));
			MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
			if (!res) return;
			while (MYSQL_ROW row = mysql_fetch_row(res))
			{
				MailItemAttachment a;
				if (row[1]) a.itemTemplateId = static_cast<uint32_t>(std::strtoul(row[1], nullptr, 10));
				if (row[2]) a.count = static_cast<uint32_t>(std::strtoul(row[2], nullptr, 10));
				m.items.push_back(a);
			}
			engine::server::db::DbFreeResult(res);
		}
	}

	uint64_t MysqlMailStore::Insert(Mail& out)
	{
		if (!m_pool || !m_pool->IsInitialized()) return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return 0;

		engine::server::db::ScopedTransaction tx(mysql);

		const std::string subj = EscapeMysql(mysql, out.subject);
		const std::string body = EscapeMysql(mysql, out.body);

		char sql[1024];
		std::snprintf(sql, sizeof(sql),
			"INSERT INTO mail (sender_account_id, receiver_account_id, subject, body, "
			"copper_gold, copper_cod, sent_ts_ms, expires_ts_ms, state) "
			"VALUES (%llu, %llu, '%s', '%s', %llu, %llu, %llu, %llu, %u)",
			static_cast<unsigned long long>(out.senderAccountId),
			static_cast<unsigned long long>(out.receiverAccountId),
			subj.c_str(),
			body.c_str(),
			static_cast<unsigned long long>(out.copperGold),
			static_cast<unsigned long long>(out.copperCod),
			static_cast<unsigned long long>(out.sentTsMs),
			static_cast<unsigned long long>(out.expiresTsMs),
			static_cast<unsigned>(out.state));

		if (!engine::server::db::DbExecute(mysql, sql))
		{
			LOG_ERROR(Core, "[MysqlMailStore] Insert mail failed");
			return 0;
		}
		out.mailId = mysql_insert_id(mysql);

		for (size_t i = 0; i < out.items.size(); ++i)
		{
			const auto& it = out.items[i];
			char sqli[256];
			std::snprintf(sqli, sizeof(sqli),
				"INSERT INTO mail_items (mail_id, slot, item_template_id, count) "
				"VALUES (%llu, %zu, %u, %u)",
				static_cast<unsigned long long>(out.mailId),
				i, it.itemTemplateId, it.count);
			if (!engine::server::db::DbExecute(mysql, sqli))
			{
				LOG_ERROR(Core, "[MysqlMailStore] Insert mail_item slot={} failed", i);
				return 0;
			}
		}

		tx.Commit();
		return out.mailId;
	}

	std::optional<Mail> MysqlMailStore::Find(uint64_t mailId) const
	{
		if (!m_pool || !m_pool->IsInitialized()) return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return std::nullopt;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT mail_id, sender_account_id, receiver_account_id, subject, body, "
			"copper_gold, copper_cod, sent_ts_ms, expires_ts_ms, state "
			"FROM mail WHERE mail_id = %llu LIMIT 1",
			static_cast<unsigned long long>(mailId));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return std::nullopt;
		std::optional<Mail> out;
		if (MYSQL_ROW row = mysql_fetch_row(res))
		{
			Mail m = RowToMail(row);
			engine::server::db::DbFreeResult(res);
			LoadItemsForMail(mysql, m);
			return m;
		}
		engine::server::db::DbFreeResult(res);
		return std::nullopt;
	}

	std::vector<Mail> MysqlMailStore::ListInbox(uint64_t receiverAccountId) const
	{
		std::vector<Mail> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT mail_id, sender_account_id, receiver_account_id, subject, body, "
			"copper_gold, copper_cod, sent_ts_ms, expires_ts_ms, state "
			"FROM mail WHERE receiver_account_id = %llu",
			static_cast<unsigned long long>(receiverAccountId));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return out;
		while (MYSQL_ROW row = mysql_fetch_row(res))
			out.push_back(RowToMail(row));
		engine::server::db::DbFreeResult(res);

		for (auto& m : out)
			LoadItemsForMail(mysql, m);
		return out;
	}

	bool MysqlMailStore::Update(const Mail& mail)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		engine::server::db::ScopedTransaction tx(mysql);

		// Update main row.
		char sql[512];
		std::snprintf(sql, sizeof(sql),
			"UPDATE mail SET copper_gold = %llu, copper_cod = %llu, state = %u, "
			"expires_ts_ms = %llu WHERE mail_id = %llu",
			static_cast<unsigned long long>(mail.copperGold),
			static_cast<unsigned long long>(mail.copperCod),
			static_cast<unsigned>(mail.state),
			static_cast<unsigned long long>(mail.expiresTsMs),
			static_cast<unsigned long long>(mail.mailId));
		if (!engine::server::db::DbExecute(mysql, sql)) return false;

		// Items : delete + reinsert (simple). Une optimisation possible
		// est diff, mais le caller appelle Update apres TakeItems donc
		// items est vide ou peu nombreux.
		std::snprintf(sql, sizeof(sql),
			"DELETE FROM mail_items WHERE mail_id = %llu",
			static_cast<unsigned long long>(mail.mailId));
		engine::server::db::DbExecute(mysql, sql);

		for (size_t i = 0; i < mail.items.size(); ++i)
		{
			const auto& it = mail.items[i];
			std::snprintf(sql, sizeof(sql),
				"INSERT INTO mail_items (mail_id, slot, item_template_id, count) "
				"VALUES (%llu, %zu, %u, %u)",
				static_cast<unsigned long long>(mail.mailId),
				i, it.itemTemplateId, it.count);
			if (!engine::server::db::DbExecute(mysql, sql)) return false;
		}
		tx.Commit();
		return true;
	}

	bool MysqlMailStore::Delete(uint64_t mailId)
	{
		if (!m_pool || !m_pool->IsInitialized()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return false;

		engine::server::db::ScopedTransaction tx(mysql);
		char sql[128];
		std::snprintf(sql, sizeof(sql),
			"DELETE FROM mail_items WHERE mail_id = %llu",
			static_cast<unsigned long long>(mailId));
		engine::server::db::DbExecute(mysql, sql);

		std::snprintf(sql, sizeof(sql),
			"DELETE FROM mail WHERE mail_id = %llu",
			static_cast<unsigned long long>(mailId));
		const bool ok = engine::server::db::DbExecute(mysql, sql);
		if (ok) tx.Commit();
		return ok;
	}

	std::vector<uint64_t> MysqlMailStore::FindExpired(uint64_t nowMs) const
	{
		std::vector<uint64_t> out;
		if (!m_pool || !m_pool->IsInitialized()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		if (!mysql) return out;

		char sql[256];
		std::snprintf(sql, sizeof(sql),
			"SELECT mail_id FROM mail WHERE expires_ts_ms > 0 AND expires_ts_ms <= %llu",
			static_cast<unsigned long long>(nowMs));
		MYSQL_RES* res = engine::server::db::DbQuery(mysql, sql);
		if (!res) return out;
		while (MYSQL_ROW row = mysql_fetch_row(res))
		{
			if (row[0]) out.push_back(std::strtoull(row[0], nullptr, 10));
		}
		engine::server::db::DbFreeResult(res);
		return out;
	}
}
