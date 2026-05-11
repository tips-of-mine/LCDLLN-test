#include "src/masterd/mail/MailManager.h"

#include <algorithm>

namespace engine::server::mail
{
	MailManager::MailManager(IMailStore* store, MailManagerConfig cfg)
		: m_store(store), m_cfg(cfg) {}

	uint64_t MailManager::Send(uint64_t senderAccountId, uint64_t receiverAccountId,
		std::string_view subject, std::string_view body,
		std::vector<MailItemAttachment> items,
		uint64_t copperGold, uint64_t copperCod,
		uint64_t nowMs, uint64_t expiresTsMs,
		MailOpResult* outErr)
	{
		if (!m_store) return 0;

		if (subject.size() > kMaxMailSubjectBytes)
		{
			if (outErr) *outErr = MailOpResult::SubjectTooLong;
			return 0;
		}
		if (body.size() > kMaxMailBodyBytes)
		{
			if (outErr) *outErr = MailOpResult::BodyTooLong;
			return 0;
		}
		if (items.size() > kMaxMailAttachments)
		{
			if (outErr) *outErr = MailOpResult::AttachmentsTooMany;
			return 0;
		}

		Mail m;
		m.senderAccountId   = senderAccountId;
		m.receiverAccountId = receiverAccountId;
		m.subject           = std::string(subject);
		m.body              = std::string(body);
		m.items             = std::move(items);
		m.copperGold        = copperGold;
		m.copperCod         = copperCod;
		m.sentTsMs          = nowMs;
		m.expiresTsMs       = (expiresTsMs > 0) ? expiresTsMs
			: (nowMs + m_cfg.defaultExpirationMs);
		m.state             = MailState::Unread;

		if (outErr) *outErr = MailOpResult::OK;
		return m_store->Insert(m);
	}

	MailOpResult MailManager::MarkRead(uint64_t mailId, uint64_t receiverAccountId)
	{
		if (!m_store) return MailOpResult::MailNotFound;
		auto opt = m_store->Find(mailId);
		if (!opt) return MailOpResult::MailNotFound;
		if (opt->receiverAccountId != receiverAccountId)
			return MailOpResult::WrongReceiver;
		if (opt->state == MailState::Unread)
		{
			opt->state = MailState::Read;
			m_store->Update(*opt);
		}
		return MailOpResult::OK;
	}

	MailOpResult MailManager::TakeItems(uint64_t mailId, uint64_t receiverAccountId,
		uint64_t paidCopper, std::vector<MailItemAttachment>& outItems)
	{
		outItems.clear();
		if (!m_store) return MailOpResult::MailNotFound;
		auto opt = m_store->Find(mailId);
		if (!opt) return MailOpResult::MailNotFound;
		if (opt->receiverAccountId != receiverAccountId)
			return MailOpResult::WrongReceiver;
		if (opt->items.empty()) return MailOpResult::AlreadyTaken;
		if (opt->copperCod > 0 && paidCopper < opt->copperCod)
			return MailOpResult::CodNotPaid;

		outItems = std::move(opt->items);
		opt->items.clear();
		// Une fois COD paye et items retires, on remet le COD a 0 pour
		// eviter un double prelevement si le caller re-Take.
		opt->copperCod = 0;
		// Marque comme lu si pas deja.
		if (opt->state == MailState::Unread)
			opt->state = MailState::Read;
		m_store->Update(*opt);
		return MailOpResult::OK;
	}

	MailOpResult MailManager::TakeGold(uint64_t mailId, uint64_t receiverAccountId,
		uint64_t& outCopper)
	{
		outCopper = 0;
		if (!m_store) return MailOpResult::MailNotFound;
		auto opt = m_store->Find(mailId);
		if (!opt) return MailOpResult::MailNotFound;
		if (opt->receiverAccountId != receiverAccountId)
			return MailOpResult::WrongReceiver;
		if (opt->copperGold == 0) return MailOpResult::AlreadyTaken;

		outCopper = opt->copperGold;
		opt->copperGold = 0;
		if (opt->state == MailState::Unread)
			opt->state = MailState::Read;
		m_store->Update(*opt);
		return MailOpResult::OK;
	}

	MailOpResult MailManager::Delete(uint64_t mailId, uint64_t receiverAccountId)
	{
		if (!m_store) return MailOpResult::MailNotFound;
		auto opt = m_store->Find(mailId);
		if (!opt) return MailOpResult::MailNotFound;
		if (opt->receiverAccountId != receiverAccountId)
			return MailOpResult::WrongReceiver;
		if (!opt->items.empty() || opt->copperGold > 0)
			return MailOpResult::AlreadyTaken;  // require Take avant Delete
		m_store->Delete(mailId);
		return MailOpResult::OK;
	}

	std::vector<Mail> MailManager::Inbox(uint64_t receiverAccountId) const
	{
		if (!m_store) return {};
		auto v = m_store->ListInbox(receiverAccountId);
		// Tri par sentTsMs decroissant (plus recent d'abord).
		std::sort(v.begin(), v.end(),
			[](const Mail& a, const Mail& b) { return a.sentTsMs > b.sentTsMs; });
		return v;
	}

	std::vector<uint64_t> MailManager::ResolveExpired(uint64_t nowMs) const
	{
		if (!m_store) return {};
		return m_store->FindExpired(nowMs);
	}
}
