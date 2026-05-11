#pragma once
// Wave 11 Persistence Cinematics — MysqlCinematicStore : impl MySQL
// d'ICinematicStore (table cinematic_seen, migration 0058). Cible UNIX
// (master Linux). Best-effort : tout echec query logge un warning mais ne
// leve pas d'exception. MarkSeen est idempotent via INSERT IGNORE.

#include "src/masterd/cinematics/CinematicStore.h"

namespace engine::server::db { class ConnectionPool; }

namespace engine::server::cinematics
{
	class MysqlCinematicStore final : public ICinematicStore
	{
	public:
		explicit MysqlCinematicStore(engine::server::db::ConnectionPool* pool)
			: m_pool(pool) {}

		/// True si le pool est initialise (mode DB actif). Aligne sur le
		/// pattern Wave 5 (MysqlGuildStore::IsAvailable).
		bool IsAvailable() const noexcept;

		bool MarkSeen(uint64_t accountId, uint32_t sequenceId, uint64_t nowMs) override;
		bool HasSeen(uint64_t accountId, uint32_t sequenceId) const override;
		std::vector<uint32_t> ListSeen(uint64_t accountId) const override;

	private:
		engine::server::db::ConnectionPool* m_pool = nullptr;
	};
}
