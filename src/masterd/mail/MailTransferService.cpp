// ETAPE5.6 — côté master de l'or joint à un courrier. Voir MailTransferService.h.

#include "src/masterd/mail/MailTransferService.h"

#include "src/shared/core/Log.h"
#include "src/shared/db/ConnectionPool.h"
#include "src/shared/db/DbHelpers.h"
#include "src/shared/db/SqlPreparedStatement.h"

#include <mysql.h>

namespace engine::server
{
	namespace
	{
		constexpr const char* kSelectColumns =
			"SELECT operation_id, mail_id, sender_account_id, sender_character_id, sender_shard_id, "
			"receiver_account_id, gold, state, deadline_ms FROM mail_transfers ";

		MailTransferRow ReadRow(db::SqlPreparedStatement& st)
		{
			MailTransferRow r;
			r.operationId = st.GetString(0);
			r.mailId = st.GetUInt64(1);
			r.senderAccountId = st.GetUInt64(2);
			r.senderCharacterId = st.GetUInt64(3);
			r.senderShardId = static_cast<uint32_t>(st.GetUInt64(4));
			r.receiverAccountId = st.GetUInt64(5);
			r.gold = st.GetUInt64(6);
			r.state = engine::mail::ParseMailTransferState(st.GetString(7)).value_or(engine::mail::MailTransferState::Pending);
			r.deadlineMs = st.GetUInt64(8);
			return r;
		}
	}

	bool MailTransferService::IsAvailable() const
	{
		return m_pool != nullptr && m_pool->IsInitialized();
	}

	bool MailTransferService::CreatePending(MailTransferRow row)
	{
		if (!IsAvailable() || row.mailId == 0u || row.gold == 0u)
			return false;
		row.operationId = engine::mail::MakeMailTransferOperationId(row.mailId);
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		auto* st = cache->Acquire(mysql,
			"INSERT INTO mail_transfers (operation_id, mail_id, sender_account_id, sender_character_id, "
			"sender_shard_id, receiver_account_id, gold, state, deadline_ms) VALUES (?, ?, ?, ?, ?, ?, ?, 'pending', ?)");
		const bool ok = st != nullptr && st->Bind(0, std::string_view(row.operationId)) && st->Bind(1, row.mailId)
			&& st->Bind(2, row.senderAccountId) && st->Bind(3, row.senderCharacterId)
			&& st->Bind(4, row.senderShardId) && st->Bind(5, row.receiverAccountId)
			&& st->Bind(6, row.gold) && st->Bind(7, row.deadlineMs) && st->Execute();
		if (!ok)
			LOG_WARN(Net, "[MailTransfer] CreatePending échoué (mail_id={})", row.mailId);
		return ok;
	}

	std::optional<MailTransferRow> MailTransferService::FindByOperation(const std::string& operationId) const
	{
		if (!IsAvailable())
			return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return std::nullopt;
		auto* st = cache->Acquire(mysql, std::string(kSelectColumns) + "WHERE operation_id = ?");
		if (st == nullptr || !st->Bind(0, std::string_view(operationId)) || !st->Execute() || !st->FetchRow())
			return std::nullopt;
		MailTransferRow r = ReadRow(*st);
		while (st->FetchRow()) {}
		return r;
	}

	std::optional<MailTransferRow> MailTransferService::FindByMail(uint64_t mailId) const
	{
		if (!IsAvailable())
			return std::nullopt;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return std::nullopt;
		auto* st = cache->Acquire(mysql, std::string(kSelectColumns) + "WHERE mail_id = ?");
		if (st == nullptr || !st->Bind(0, mailId) || !st->Execute() || !st->FetchRow())
			return std::nullopt;
		MailTransferRow r = ReadRow(*st);
		while (st->FetchRow()) {}
		return r;
	}

	bool MailTransferService::CancelPending(const std::string& operationId)
	{
		if (!IsAvailable())
			return false;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return false;
		auto* st = cache->Acquire(mysql,
			"UPDATE mail_transfers SET state = 'cancelled' WHERE operation_id = ? AND state = 'pending'");
		return st != nullptr && st->Bind(0, std::string_view(operationId)) && st->Execute() && st->AffectedRows() == 1u;
	}

	MailDeliverResult MailTransferService::Deliver(uint64_t mailId, uint64_t receiverAccountId, uint64_t receiverCharacterId)
	{
		if (!IsAvailable() || receiverCharacterId == 0u)
			return MailDeliverResult::Failed;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return MailDeliverResult::Failed;
		db::ScopedTransaction tx(mysql);
		if (!tx.IsStarted())
			return MailDeliverResult::Failed;
		auto* sel = cache->Acquire(mysql, std::string(kSelectColumns) + "WHERE mail_id = ? FOR UPDATE");
		if (sel == nullptr || !sel->Bind(0, mailId) || !sel->Execute())
			return MailDeliverResult::Failed;
		if (!sel->FetchRow())
			return MailDeliverResult::NoTransfer;
		const MailTransferRow row = ReadRow(*sel);
		while (sel->FetchRow()) {}
		if (row.receiverAccountId != receiverAccountId)
			return MailDeliverResult::NoTransfer;
		using engine::mail::MailTransferState;
		if (row.state == MailTransferState::Delivered)
			return MailDeliverResult::AlreadyDelivered;
		if (row.state == MailTransferState::Pending)
			return MailDeliverResult::Pending;
		if (row.state != MailTransferState::Locked)
			return MailDeliverResult::Cancelled;
		auto* up = cache->Acquire(mysql,
			"UPDATE mail_transfers SET state = 'delivered', receiver_character_id = ? "
			"WHERE operation_id = ? AND state = 'locked'");
		if (up == nullptr || !up->Bind(0, receiverCharacterId) || !up->Bind(1, std::string_view(row.operationId))
			|| !up->Execute() || up->AffectedRows() != 1u)
			return MailDeliverResult::Failed;
		// Dépôt (une seule fois : le passage locked → delivered ci-dessus en est la garde ;
		// l'identifiant unique protège en plus contre un double dépôt).
		const std::string depositId = engine::mail::MakeMailboxDepositOperationId(row.operationId, "deliver");
		auto* ins = cache->Acquire(mysql,
			"INSERT INTO character_mailbox (character_id, gold, item_id, quantity, source, operation_id) "
			"VALUES (?, ?, 0, 0, 'mail', ?)");
		if (ins == nullptr || !ins->Bind(0, receiverCharacterId) || !ins->Bind(1, row.gold)
			|| !ins->Bind(2, std::string_view(depositId)) || !ins->Execute() || ins->AffectedRows() != 1u)
			return MailDeliverResult::Failed; // ScopedTransaction annule
		if (!tx.Commit())
			return MailDeliverResult::Failed;
		LOG_INFO(Net, "[MailTransfer] or livré (operation={}, receiver_character={}, gold={})",
			row.operationId, receiverCharacterId, row.gold);
		return MailDeliverResult::Delivered;
	}

	std::vector<MailTransferRow> MailTransferService::ListExpiredPending(uint64_t nowMs, size_t limit) const
	{
		std::vector<MailTransferRow> out;
		if (!IsAvailable())
			return out;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return out;
		auto* st = cache->Acquire(mysql, std::string(kSelectColumns)
			+ "WHERE state = 'pending' AND deadline_ms <= ? ORDER BY deadline_ms LIMIT ?");
		if (st == nullptr || !st->Bind(0, nowMs) || !st->Bind(1, static_cast<uint64_t>(limit)) || !st->Execute())
			return out;
		while (st->FetchRow())
			out.push_back(ReadRow(*st));
		return out;
	}

	size_t MailTransferService::RefundExpiredMails(uint64_t nowMs, size_t limit)
	{
		if (!IsAvailable())
			return 0;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return 0;
		// Courriers expirés (ou supprimés) dont l'or est encore en dépôt ou en attente.
		std::vector<MailTransferRow> rows;
		{
			auto* st = cache->Acquire(mysql,
				"SELECT t.operation_id, t.mail_id, t.sender_account_id, t.sender_character_id, t.sender_shard_id, "
				"t.receiver_account_id, t.gold, t.state, t.deadline_ms FROM mail_transfers t "
				"LEFT JOIN mail m ON m.mail_id = t.mail_id "
				"WHERE t.state IN ('pending', 'locked') "
				"AND (m.mail_id IS NULL OR (m.expires_ts_ms > 0 AND m.expires_ts_ms < ?) OR m.state = 3) LIMIT ?");
			if (st == nullptr || !st->Bind(0, nowMs) || !st->Bind(1, static_cast<uint64_t>(limit)) || !st->Execute())
				return 0;
			while (st->FetchRow())
				rows.push_back(ReadRow(*st));
		}
		size_t refunded = 0;
		for (const MailTransferRow& row : rows)
		{
			if (row.state == engine::mail::MailTransferState::Pending)
			{
				auto* c = cache->Acquire(mysql,
					"UPDATE mail_transfers SET state = 'cancelled' WHERE operation_id = ? AND state = 'pending'");
				if (c != nullptr && c->Bind(0, std::string_view(row.operationId)))
					(void)c->Execute();
				continue;
			}
			db::ScopedTransaction tx(mysql);
			if (!tx.IsStarted())
				continue;
			auto* up = cache->Acquire(mysql,
				"UPDATE mail_transfers SET state = 'refunded' WHERE operation_id = ? AND state = 'locked'");
			if (up == nullptr || !up->Bind(0, std::string_view(row.operationId)) || !up->Execute() || up->AffectedRows() != 1u)
				continue; // déjà livré ou rendu entre-temps
			const std::string depositId = engine::mail::MakeMailboxDepositOperationId(row.operationId, "refund");
			auto* ins = cache->Acquire(mysql,
				"INSERT INTO character_mailbox (character_id, gold, item_id, quantity, source, operation_id) "
				"VALUES (?, ?, 0, 0, 'mail_refund', ?)");
			if (ins == nullptr || !ins->Bind(0, row.senderCharacterId) || !ins->Bind(1, row.gold)
				|| !ins->Bind(2, std::string_view(depositId)) || !ins->Execute() || ins->AffectedRows() != 1u)
				continue; // ScopedTransaction annule
			if (tx.Commit())
			{
				++refunded;
				LOG_INFO(Net, "[MailTransfer] or rendu à l'expéditeur (operation={}, character={}, gold={})",
					row.operationId, row.senderCharacterId, row.gold);
			}
		}
		return refunded;
	}

	MailDeliverResult MailTransferService::DepositSystemMail(uint64_t mailId, uint64_t receiverAccountId,
		uint64_t receiverCharacterId, uint64_t gold, const std::vector<std::pair<uint32_t, uint32_t>>& items)
	{
		if (!IsAvailable() || receiverCharacterId == 0u || mailId == 0u)
			return MailDeliverResult::Failed;
		if (gold > 0xFFFFFFFFull)
			gold = 0xFFFFFFFFull;
		auto guard = m_pool->Acquire();
		MYSQL* mysql = guard.get();
		auto* cache = guard.cache();
		if (!mysql || !cache)
			return MailDeliverResult::Failed;
		db::ScopedTransaction tx(mysql);
		if (!tx.IsStarted())
			return MailDeliverResult::Failed;
		const std::string op = engine::mail::MakeMailTransferOperationId(mailId);
		// Garde durable : la ligne de transfert « delivered » (clé primaire). Les dépôts de
		// character_mailbox disparaissent une fois crédités ; elle reste.
		auto* sel = cache->Acquire(mysql, "SELECT state FROM mail_transfers WHERE operation_id = ? FOR UPDATE");
		if (sel == nullptr || !sel->Bind(0, std::string_view(op)) || !sel->Execute())
			return MailDeliverResult::Failed;
		if (sel->FetchRow())
		{
			while (sel->FetchRow()) {}
			return MailDeliverResult::AlreadyDelivered;
		}
		auto* mark = cache->Acquire(mysql,
			"INSERT INTO mail_transfers (operation_id, mail_id, sender_account_id, sender_character_id, sender_shard_id, "
			"receiver_account_id, gold, state, deadline_ms, receiver_character_id) VALUES (?, ?, 0, 0, 0, ?, ?, 'delivered', 0, ?)");
		if (mark == nullptr || !mark->Bind(0, std::string_view(op)) || !mark->Bind(1, mailId) || !mark->Bind(2, receiverAccountId)
			|| !mark->Bind(3, gold) || !mark->Bind(4, receiverCharacterId) || !mark->Execute() || mark->AffectedRows() != 1u)
			return MailDeliverResult::Failed;
		auto deposit = [&](uint64_t g, uint32_t itemId, uint32_t quantity, const std::string& depositId) {
			auto* ins = cache->Acquire(mysql,
				"INSERT INTO character_mailbox (character_id, gold, item_id, quantity, source, operation_id) "
				"VALUES (?, ?, ?, ?, 'mail_system', ?)");
			return ins != nullptr && ins->Bind(0, receiverCharacterId) && ins->Bind(1, g) && ins->Bind(2, itemId)
				&& ins->Bind(3, quantity) && ins->Bind(4, std::string_view(depositId)) && ins->Execute()
				&& ins->AffectedRows() == 1u;
		};
		if (gold > 0u && !deposit(gold, 0u, 0u, engine::mail::MakeMailboxDepositOperationId(op, "gold")))
			return MailDeliverResult::Failed;
		for (size_t i = 0; i < items.size(); ++i)
		{
			if (items[i].first == 0u || items[i].second == 0u)
				continue;
			if (!deposit(0u, items[i].first, items[i].second,
				engine::mail::MakeMailboxDepositOperationId(op, "item" + std::to_string(i))))
				return MailDeliverResult::Failed;
		}
		if (!tx.Commit())
			return MailDeliverResult::Failed;
		LOG_INFO(Net, "[MailTransfer] courrier système crédité (mail_id={}, character={}, gold={}, objets={})",
			mailId, receiverCharacterId, gold, items.size());
		return MailDeliverResult::Delivered;
	}
}
