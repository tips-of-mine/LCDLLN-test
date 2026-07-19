#pragma once
// CMANGOS.18 (Phase 3.18b) — MysqlMailStore : implementation MySQL
// d'IMailStore pour persistance production. Cible UNIX uniquement
// (le shard WIN32 utilise InMemoryMailStore par défaut).

#include "src/masterd/mail/MailManager.h"

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::mail
{
	class MysqlMailStore final : public IMailStore
	{
	public:
		explicit MysqlMailStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Retourne true si le store est en mode DB (pool initialise).
		/// Le caller s'en sert pour decider d'instancier le MailManager
		/// avec ce store ou avec un InMemoryMailStore (mode no-DB / tests).
		bool IsAvailable() const noexcept;

		/// Roadmap-4 (2026-07-19) — purge les courriers EXPIRÉS
		/// (expires_ts_ms > 0 et < \p nowMs — l'expiration vaut disparition,
		/// pièces jointes comprises) et les courriers SUPPRIMÉS par leur
		/// destinataire (state=Deleted). mail_items d'abord (pas de FK
		/// cascade), puis mail. \return nombre de courriers supprimés.
		size_t PurgeExpired(uint64_t nowMs);

		uint64_t Insert(Mail& out) override;
		std::optional<Mail> Find(uint64_t mailId) const override;
		std::vector<Mail> ListInbox(uint64_t receiverAccountId) const override;
		bool Update(const Mail& mail) override;
		bool Delete(uint64_t mailId) override;
		std::vector<uint64_t> FindExpired(uint64_t nowMs) const override;

	private:
		engine::server::db::ConnectionPool* m_pool;
	};
}
