#pragma once
// CMANGOS.32 (Phase 5.32a) — GmTicketSystem : queue de tickets joueurs
// pour le support GM. Header-only.
//
// Audit 2026-06-10 (Lot B1) — THREAD-SAFE : les handlers du master sont
// dispatchés sur un pool de workers NetServer (défaut 4) ; chaque méthode
// publique verrouille m_mutex (aucune méthode publique n'en appelle une autre).

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::server::gmtickets
{
	using TicketId  = uint64_t;
	using AccountId = uint64_t;

	enum class TicketState : uint8_t
	{
		Open       = 0,
		Assigned   = 1,
		Resolved   = 2,
		Cancelled  = 3,
	};

	struct GmTicket
	{
		TicketId    id            = 0;
		AccountId   reporter      = 0;
		std::string body;
		uint64_t    createdTsMs   = 0;
		uint64_t    resolvedTsMs  = 0;
		AccountId   assignedGm    = 0;
		TicketState state         = TicketState::Open;
	};

	class GmTicketSystem
	{
	public:
		TicketId Open(AccountId reporter, std::string body, uint64_t nowMs)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			GmTicket t;
			t.id           = m_nextId++;
			t.reporter     = reporter;
			t.body         = std::move(body);
			t.createdTsMs  = nowMs;
			m_tickets[t.id] = std::move(t);
			return m_tickets[t.id].id;
		}

		bool Assign(TicketId id, AccountId gm)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_tickets.find(id);
			if (it == m_tickets.end()) return false;
			if (it->second.state != TicketState::Open) return false;
			it->second.assignedGm = gm;
			it->second.state = TicketState::Assigned;
			return true;
		}

		bool Resolve(TicketId id, uint64_t nowMs)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_tickets.find(id);
			if (it == m_tickets.end()) return false;
			if (it->second.state == TicketState::Resolved
				|| it->second.state == TicketState::Cancelled)
				return false;
			it->second.state = TicketState::Resolved;
			it->second.resolvedTsMs = nowMs;
			return true;
		}

		bool Cancel(TicketId id)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_tickets.find(id);
			if (it == m_tickets.end()) return false;
			it->second.state = TicketState::Cancelled;
			return true;
		}

		std::optional<GmTicket> Find(TicketId id) const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_tickets.find(id);
			return (it == m_tickets.end()) ? std::nullopt : std::optional<GmTicket>(it->second);
		}

		std::vector<GmTicket> OpenQueue() const
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			std::vector<GmTicket> out;
			for (const auto& [id, t] : m_tickets)
				if (t.state == TicketState::Open) out.push_back(t);
			return out;
		}

	private:
		/// Audit Lot B1 — protège m_tickets/m_nextId contre les workers concurrents.
		mutable std::mutex m_mutex;
		std::unordered_map<TicketId, GmTicket> m_tickets;
		TicketId m_nextId = 1;
	};
}
