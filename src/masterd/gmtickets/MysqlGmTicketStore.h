#pragma once
// CMANGOS.32 (Phase 5.32b) — MysqlGmTicketStore : persistance MySQL des
// tickets GM (table gm_tickets, migration 0046). Cible UNIX shard.
// Le WIN32 sandbox utilise GmTicketSystem (header-only en memoire).

#include "src/masterd/gmtickets/GmTicketSystem.h"

#include <optional>
#include <vector>

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::gmtickets
{
	class MysqlGmTicketStore
	{
	public:
		explicit MysqlGmTicketStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// Insert : remplit \p out.id avec l'AUTO_INCREMENT genere par MySQL.
		/// Retourne 0 en cas d'echec.
		uint64_t Insert(GmTicket& out);

		/// Update tous les champs mutables (state, assignedGm, resolvedTsMs, body).
		bool Update(const GmTicket& t);

		bool Delete(TicketId id);
		std::optional<GmTicket> Find(TicketId id) const;

		/// Tous les tickets dans state == Open (queue support GM).
		std::vector<GmTicket> ListOpen() const;

	private:
		engine::server::db::ConnectionPool* m_pool;
	};
}
