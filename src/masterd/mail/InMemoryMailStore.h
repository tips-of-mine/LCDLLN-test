#pragma once
// CMANGOS.18 (Phase 3.18a) — InMemoryMailStore : implementation RAM
// d'IMailStore pour les tests + fallback dev sans MySQL.

#include "src/masterd/mail/MailManager.h"

#include <unordered_map>

namespace engine::server::mail
{
	class InMemoryMailStore final : public IMailStore
	{
	public:
		uint64_t Insert(Mail& out) override
		{
			out.mailId = m_nextId++;
			m_mails[out.mailId] = out;
			return out.mailId;
		}

		std::optional<Mail> Find(uint64_t mailId) const override
		{
			auto it = m_mails.find(mailId);
			return (it == m_mails.end()) ? std::nullopt : std::optional<Mail>(it->second);
		}

		std::vector<Mail> ListInbox(uint64_t receiverAccountId) const override
		{
			std::vector<Mail> out;
			for (const auto& [id, m] : m_mails)
			{
				if (m.receiverAccountId == receiverAccountId)
					out.push_back(m);
			}
			return out;
		}

		bool Update(const Mail& mail) override
		{
			auto it = m_mails.find(mail.mailId);
			if (it == m_mails.end()) return false;
			it->second = mail;
			return true;
		}

		bool Delete(uint64_t mailId) override
		{
			return m_mails.erase(mailId) > 0;
		}

		std::vector<uint64_t> FindExpired(uint64_t nowMs) const override
		{
			std::vector<uint64_t> out;
			for (const auto& [id, m] : m_mails)
			{
				if (m.expiresTsMs > 0 && m.expiresTsMs <= nowMs)
					out.push_back(id);
			}
			return out;
		}

		size_t Size() const noexcept { return m_mails.size(); }

	private:
		std::unordered_map<uint64_t, Mail> m_mails;
		uint64_t m_nextId = 1;
	};
}
