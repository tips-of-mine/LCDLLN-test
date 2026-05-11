#pragma once
// Wave 11 Persistence Cinematics — InMemoryCinematicStore : impl RAM
// d'ICinematicStore pour dev / tests / fallback no-DB. L'etat est perdu au
// reboot du master ; le client reverra l'intro a chaque relance dans ce mode.

#include "src/masterd/cinematics/CinematicStore.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace engine::server::cinematics
{
	/// Map accountId -> set<sequenceId>. N reste petit en V1 (qq accounts,
	/// qq dizaines de cinematics) donc on garde la struct lisible plutot que
	/// d'optimiser via un pack 64 bits (accountId<<32)|sequenceId.
	/// Thread-safe via un mutex unique : volumes V1 negligeables, on
	/// privilegie la simplicite.
	class InMemoryCinematicStore final : public ICinematicStore
	{
	public:
		bool MarkSeen(uint64_t accountId, uint32_t sequenceId, uint64_t nowMs) override;
		bool HasSeen(uint64_t accountId, uint32_t sequenceId) const override;
		std::vector<uint32_t> ListSeen(uint64_t accountId) const override;

	private:
		mutable std::mutex                                          m_mutex;
		std::unordered_map<uint64_t, std::unordered_set<uint32_t>>  m_seen;
	};
}
