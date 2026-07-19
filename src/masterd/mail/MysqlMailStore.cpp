// N1-H : converti en prepared statements (Insert+Update avec transaction,
// Delete, Find+LoadItemsForMail, ListInbox, FindExpired).

#include "src/masterd/mail/MysqlMailStore.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

#include <algorithm>

namespace engine::server::mail
{
	namespace
	{
		/// Mappe le résultat courant d'un SqlPreparedStatement (10 colonnes
		/// dans l'ordre canonique mail_id, sender, receiver, subject, body,
		/// copper_gold, copper_cod, sent_ts_ms, expires_ts_ms, state) vers Mail.
		/// N'attache PAS les items — c'est LoadItemsForMail qui s'en charge.
		Mail StmtToMail(engine::server::db::SqlPreparedStatement* stmt)
		{
			Mail m;
			m.mailId             = stmt->GetUInt64(0);
			m.senderAccountId    = stmt->GetUInt64(1);
			m.receiverAccountId  = stmt->GetUInt64(2);
			m.subject            = stmt->GetString(3);
			m.body               = stmt->GetString(4);
			m.copperGold         = stmt->GetUInt64(5);
			m.copperCod          = stmt->GetUInt64(6);
			m.sentTsMs           = stmt->GetUInt64(7);
			m.expiresTsMs        = stmt->GetUInt64(8);
			const uint32_t st    = static_cast<uint32_t>(stmt->GetUInt64(9));
			m.state              = static_cast<MailState>(std::min<uint32_t>(st, 3));
			return m;
		}

		/// Charge les attachments d'un mail depuis mail_items dans m (append-only).
		/// N1-H : prepared statement.
		void LoadItemsForMail(MYSQL* mysql, engine::server::db::SqlPreparedStatementCache* cache, Mail& m)
		{
			auto* stmt = cache->Acquire(mysql,
				"SELECT slot, item_template_id, count FROM mail_items "
				"WHERE mail_id = ? ORDER BY slot ASC");
			if (!stmt || !stmt->Bind(0, m.mailId) || !stmt->Execute())
			{
				LOG_WARN(Net, "[MysqlMailStore] LoadItemsForMail query failed mailId={}", m.mailId);
				return;
			}
			while (stmt->FetchRow())
			{
				MailItemAttachment a;
				a.itemTemplateId = static_cast<uint32_t>(stmt->GetUInt64(1));
				a.count          = static_cast<uint32_t>(stmt->GetUInt64(2));
				m.items.push_back(a);
			}
		}
	}

	bool MysqlMailStore::IsAvailable() const noexcept
	{
		return m_pool && m_pool->IsInitialized();
	}

	size_t MysqlMailStore::PurgeExpired(uint64_t nowMs)
	{
		if (!IsAvailable()) return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return 0;

		engine::server::db::ScopedTransaction tx(mysql);

		// 1. Pièces jointes des courriers à purger (pas de FK ON DELETE
		// CASCADE sur mail_items — migration 0045).
		auto* itemsStmt = cache->Acquire(mysql,
			"DELETE mi FROM mail_items mi JOIN mail m ON m.mail_id = mi.mail_id "
			"WHERE (m.expires_ts_ms > 0 AND m.expires_ts_ms < ?) OR m.state = 3");
		if (!itemsStmt || !itemsStmt->Bind(0, nowMs) || !itemsStmt->Execute())
		{
			LOG_WARN(Net, "[MysqlMailStore] PurgeExpired: delete mail_items failed");
			return 0;
		}

		// 2. Les courriers eux-mêmes.
		auto* mailStmt = cache->Acquire(mysql,
			"DELETE FROM mail WHERE (expires_ts_ms > 0 AND expires_ts_ms < ?) OR state = 3");
		if (!mailStmt || !mailStmt->Bind(0, nowMs) || !mailStmt->Execute())
		{
			LOG_WARN(Net, "[MysqlMailStore] PurgeExpired: delete mail failed");
			return 0;
		}
		const size_t purged = static_cast<size_t>(mailStmt->AffectedRows());
		tx.Commit();
		if (purged > 0)
		{
			LOG_INFO(Net, "[MysqlMailStore] PurgeExpired: {} courrier(s) purgé(s)", purged);
		}
		return purged;
	}

	uint64_t MysqlMailStore::Insert(Mail& out)
	{
		if (!IsAvailable()) return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return 0;

		engine::server::db::ScopedTransaction tx(mysql);

		// INSERT mail (9 binds, subject + body strings user-controlled).
		auto* mailStmt = cache->Acquire(mysql,
			"INSERT INTO mail (sender_account_id, receiver_account_id, subject, body, "
			"copper_gold, copper_cod, sent_ts_ms, expires_ts_ms, state) "
			"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
		const bool ok = mailStmt
			&& mailStmt->Bind(0, out.senderAccountId)
			&& mailStmt->Bind(1, out.receiverAccountId)
			&& mailStmt->Bind(2, std::string_view(out.subject))
			&& mailStmt->Bind(3, std::string_view(out.body))
			&& mailStmt->Bind(4, out.copperGold)
			&& mailStmt->Bind(5, out.copperCod)
			&& mailStmt->Bind(6, out.sentTsMs)
			&& mailStmt->Bind(7, out.expiresTsMs)
			&& mailStmt->Bind(8, static_cast<uint32_t>(out.state))
			&& mailStmt->Execute();
		if (!ok)
		{
			LOG_WARN(Net, "[MysqlMailStore] Insert mail failed sender={} receiver={}",
				out.senderAccountId, out.receiverAccountId);
			return 0;
		}
		out.mailId = mysql_insert_id(mysql);

		// INSERT mail_items (1 stmt cached, ré-exécuté N fois grâce au Reset() auto).
		auto* itemStmt = cache->Acquire(mysql,
			"INSERT INTO mail_items (mail_id, slot, item_template_id, count) "
			"VALUES (?, ?, ?, ?)");
		for (size_t i = 0; i < out.items.size(); ++i)
		{
			const auto& it = out.items[i];
			// Re-Acquire pour bénéficier du Reset() automatique sur cache hit.
			itemStmt = cache->Acquire(mysql,
				"INSERT INTO mail_items (mail_id, slot, item_template_id, count) "
				"VALUES (?, ?, ?, ?)");
			const bool itemOk = itemStmt
				&& itemStmt->Bind(0, out.mailId)
				&& itemStmt->Bind(1, static_cast<uint32_t>(i))
				&& itemStmt->Bind(2, it.itemTemplateId)
				&& itemStmt->Bind(3, it.count)
				&& itemStmt->Execute();
			if (!itemOk)
			{
				LOG_WARN(Net, "[MysqlMailStore] Insert mail_item slot={} failed mailId={}",
					i, out.mailId);
				return 0;
			}
		}

		tx.Commit();
		return out.mailId;
	}

	std::optional<Mail> MysqlMailStore::Find(uint64_t mailId) const
	{
		if (!IsAvailable()) return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return std::nullopt;

		auto* stmt = cache->Acquire(mysql,
			"SELECT mail_id, sender_account_id, receiver_account_id, subject, body, "
			"copper_gold, copper_cod, sent_ts_ms, expires_ts_ms, state "
			"FROM mail WHERE mail_id = ? LIMIT 1");
		if (!stmt || !stmt->Bind(0, mailId) || !stmt->Execute() || !stmt->FetchRow())
			return std::nullopt;
		Mail m = StmtToMail(stmt);
		LoadItemsForMail(mysql, cache, m);
		return m;
	}

	std::vector<Mail> MysqlMailStore::ListInbox(uint64_t receiverAccountId) const
	{
		std::vector<Mail> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT mail_id, sender_account_id, receiver_account_id, subject, body, "
			"copper_gold, copper_cod, sent_ts_ms, expires_ts_ms, state "
			"FROM mail WHERE receiver_account_id = ? "
			"ORDER BY sent_ts_ms DESC");
		if (!stmt || !stmt->Bind(0, receiverAccountId) || !stmt->Execute())
			return out;
		while (stmt->FetchRow())
			out.push_back(StmtToMail(stmt));

		for (auto& m : out)
			LoadItemsForMail(mysql, cache, m);
		return out;
	}

	bool MysqlMailStore::Update(const Mail& mail)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		engine::server::db::ScopedTransaction tx(mysql);

		// Update main row.
		{
			auto* stmt = cache->Acquire(mysql,
				"UPDATE mail SET copper_gold = ?, copper_cod = ?, state = ?, "
				"expires_ts_ms = ? WHERE mail_id = ?");
			const bool ok = stmt
				&& stmt->Bind(0, mail.copperGold)
				&& stmt->Bind(1, mail.copperCod)
				&& stmt->Bind(2, static_cast<uint32_t>(mail.state))
				&& stmt->Bind(3, mail.expiresTsMs)
				&& stmt->Bind(4, mail.mailId)
				&& stmt->Execute();
			if (!ok)
			{
				LOG_WARN(Net, "[MysqlMailStore] Update mail row failed mailId={}", mail.mailId);
				return false;
			}
		}

		// Items : delete + reinsert (simple). Caller appelle Update apres TakeItems
		// donc items est vide ou peu nombreux.
		{
			auto* delStmt = cache->Acquire(mysql,
				"DELETE FROM mail_items WHERE mail_id = ?");
			if (delStmt && delStmt->Bind(0, mail.mailId))
				delStmt->Execute();
		}

		for (size_t i = 0; i < mail.items.size(); ++i)
		{
			const auto& it = mail.items[i];
			auto* insStmt = cache->Acquire(mysql,
				"INSERT INTO mail_items (mail_id, slot, item_template_id, count) "
				"VALUES (?, ?, ?, ?)");
			const bool ok = insStmt
				&& insStmt->Bind(0, mail.mailId)
				&& insStmt->Bind(1, static_cast<uint32_t>(i))
				&& insStmt->Bind(2, it.itemTemplateId)
				&& insStmt->Bind(3, it.count)
				&& insStmt->Execute();
			if (!ok)
			{
				LOG_WARN(Net, "[MysqlMailStore] Update mail_item slot={} failed mailId={}",
					i, mail.mailId);
				return false;
			}
		}
		tx.Commit();
		return true;
	}

	bool MysqlMailStore::Delete(uint64_t mailId)
	{
		if (!IsAvailable()) return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return false;

		engine::server::db::ScopedTransaction tx(mysql);
		{
			auto* stmt = cache->Acquire(mysql,
				"DELETE FROM mail_items WHERE mail_id = ?");
			if (stmt && stmt->Bind(0, mailId))
				stmt->Execute();
		}

		auto* mailStmt = cache->Acquire(mysql, "DELETE FROM mail WHERE mail_id = ?");
		const bool ok = mailStmt && mailStmt->Bind(0, mailId) && mailStmt->Execute();
		if (!ok)
			LOG_WARN(Net, "[MysqlMailStore] Delete mail failed mailId={}", mailId);
		else
			tx.Commit();
		return ok;
	}

	std::vector<uint64_t> MysqlMailStore::FindExpired(uint64_t nowMs) const
	{
		std::vector<uint64_t> out;
		if (!IsAvailable()) return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache) return out;

		auto* stmt = cache->Acquire(mysql,
			"SELECT mail_id FROM mail WHERE expires_ts_ms > 0 AND expires_ts_ms <= ?");
		if (!stmt || !stmt->Bind(0, nowMs) || !stmt->Execute())
			return out;
		while (stmt->FetchRow())
			out.push_back(stmt->GetUInt64(0));
		return out;
	}
}
