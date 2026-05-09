#pragma once
// CMANGOS.32 (Phase 5.32a) — GmTicketSystem : queue de tickets joueurs
// pour le support GM. Header-only.

#include <cstdint>
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
			auto it = m_tickets.find(id);
			if (it == m_tickets.end()) return false;
			if (it->second.state != TicketState::Open) return false;
			it->second.assignedGm = gm;
			it->second.state = TicketState::Assigned;
			return true;
		}

		bool Resolve(TicketId id, uint64_t nowMs)
		{
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
			auto it = m_tickets.find(id);
			if (it == m_tickets.end()) return false;
			it->second.state = TicketState::Cancelled;
			return true;
		}

		std::optional<GmTicket> Find(TicketId id) const
		{
			auto it = m_tickets.find(id);
			return (it == m_tickets.end()) ? std::nullopt : std::optional<GmTicket>(it->second);
		}

		std::vector<GmTicket> OpenQueue() const
		{
			std::vector<GmTicket> out;
			for (const auto& [id, t] : m_tickets)
				if (t.state == TicketState::Open) out.push_back(t);
			return out;
		}

	private:
		std::unordered_map<TicketId, GmTicket> m_tickets;
		TicketId m_nextId = 1;
	};
}
