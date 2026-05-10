// CMANGOS.32 (Phase 5.32 step 3+4) — Implementation GmTicketHandler.

#include "src/masterd/handlers/gmtickets/GmTicketHandler.h"

#include "src/masterd/gmtickets/GmTicketSystem.h"
#include "src/masterd/gmtickets/MysqlGmTicketStore.h"
#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/GmTicketPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <chrono>

namespace engine::server
{
	namespace
	{
		/// Retourne le timestamp UTC en ms (epoch). Sert au createdTsMs du ticket.
		uint64_t NowMs()
		{
			using namespace std::chrono;
			return static_cast<uint64_t>(
				duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
		}

		/// Verifie qu'un body de ticket n'est pas vide / blanc. Pas de full
		/// trim pour autoriser \n et tabulations utiles dans la mise en forme,
		/// mais on exige au moins un caractere visible.
		bool BodyHasContent(const std::string& body)
		{
			for (char c : body)
			{
				const unsigned char uc = static_cast<unsigned char>(c);
				if (uc > 0x20u) return true;
			}
			return false;
		}
	}

	void GmTicketHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sys || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[GmTicketHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Resolution session/account. Si l'un manque, on retourne une reponse
		// type-specific avec error=Unauthorized.
		uint64_t accountId = 0;
		bool sessionOk = false;
		auto connSessionId = m_connMap->GetSessionId(connId);
		if (connSessionId && *connSessionId != 0u
			&& sessionIdHeader != 0u && *connSessionId == sessionIdHeader)
		{
			auto acc = m_sessionMgr->GetAccountId(*connSessionId);
			if (acc && *acc != 0u)
			{
				accountId = *acc;
				sessionOk = true;
			}
		}

		if (!sessionOk)
		{
			std::vector<uint8_t> pkt;
			const uint8_t kUnauth = static_cast<uint8_t>(GmTicketErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeGmTicketOpenRequest:
				pkt = BuildGmTicketOpenResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeGmTicketListMineRequest:
				pkt = BuildGmTicketListMineResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeGmTicketCancelRequest:
				pkt = BuildGmTicketCancelResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			default:
				return;
			}
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		switch (opcode)
		{
		case kOpcodeGmTicketOpenRequest:
			HandleOpen(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeGmTicketListMineRequest:
			HandleListMine(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeGmTicketCancelRequest:
			HandleCancel(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------

	void GmTicketHandler::HandleOpen(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseGmTicketOpenRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildGmTicketOpenResponsePacket(
				static_cast<uint8_t>(GmTicketErrorCode::BodyEmpty), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Validation longueur & contenu : BodyTooLong (>4096) > BodyEmpty (vide).
		if (parsed->body.size() > kMaxGmTicketBodyBytes)
		{
			auto pkt = BuildGmTicketOpenResponsePacket(
				static_cast<uint8_t>(GmTicketErrorCode::BodyTooLong), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[GmTicketHandler] Open BodyTooLong account={} size={}",
				accountId, parsed->body.size());
			return;
		}
		if (!BodyHasContent(parsed->body))
		{
			auto pkt = BuildGmTicketOpenResponsePacket(
				static_cast<uint8_t>(GmTicketErrorCode::BodyEmpty), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[GmTicketHandler] Open BodyEmpty account={}", accountId);
			return;
		}

		const uint64_t nowMs = NowMs();
		const uint64_t newId = m_sys->Open(accountId, parsed->body, nowMs);

		// Persistance best-effort : si dispo, on insere une copie dans la table.
		// Le system in-memory reste l'autorite runtime ; la perte au reboot est
		// rattrapee lors d'un futur reload (PR ulterieure).
		if (m_store)
		{
			engine::server::gmtickets::GmTicket toInsert;
			toInsert.id           = newId;
			toInsert.reporter     = accountId;
			toInsert.body         = parsed->body;
			toInsert.createdTsMs  = nowMs;
			toInsert.state        = engine::server::gmtickets::TicketState::Open;
			const uint64_t persistedId = m_store->Insert(toInsert);
			if (persistedId == 0u)
			{
				LOG_WARN(Net, "[GmTicketHandler] Open Insert failed (account={}, ticketMem={})",
					accountId, newId);
			}
		}

		LOG_INFO(Net, "[GmTicketHandler] Open account={} newId={} bodySize={}",
			accountId, newId, parsed->body.size());

		auto pkt = BuildGmTicketOpenResponsePacket(0u, newId, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void GmTicketHandler::HandleListMine(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		// V1 : le system n'expose pas de ListByReporter dedie ; on reutilise
		// OpenQueue() (queue support runtime) et on filtre cote handler. Suffisant
		// pour M tickets ouverts << N joueurs ; si volumetrie augmente on ajoutera
		// un index inverse reporter -> tickets dans GmTicketSystem.
		const auto openTickets = m_sys->OpenQueue();
		std::vector<GmTicketEntry> entries;
		entries.reserve(openTickets.size());
		for (const auto& t : openTickets)
		{
			if (t.reporter != accountId)
				continue;
			GmTicketEntry e;
			e.id           = t.id;
			e.createdTsMs  = t.createdTsMs;
			e.resolvedTsMs = t.resolvedTsMs;
			e.state        = static_cast<uint8_t>(t.state);
			entries.push_back(e);
		}

		LOG_INFO(Net, "[GmTicketHandler] ListMine account={} count={}",
			accountId, entries.size());

		auto pkt = BuildGmTicketListMineResponsePacket(0u, entries, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	void GmTicketHandler::HandleCancel(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		auto parsed = ParseGmTicketCancelRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			auto pkt = BuildGmTicketCancelResponsePacket(
				static_cast<uint8_t>(GmTicketErrorCode::NotFound), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		const uint64_t ticketId = parsed->ticketId;
		auto found = m_sys->Find(ticketId);
		if (!found)
		{
			auto pkt = BuildGmTicketCancelResponsePacket(
				static_cast<uint8_t>(GmTicketErrorCode::NotFound), ticketId,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[GmTicketHandler] Cancel NotFound account={} ticket={}",
				accountId, ticketId);
			return;
		}

		// Ownership : seul le reporter peut cancel son ticket cote joueur.
		if (found->reporter != accountId)
		{
			auto pkt = BuildGmTicketCancelResponsePacket(
				static_cast<uint8_t>(GmTicketErrorCode::NotOwner), ticketId,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[GmTicketHandler] Cancel NotOwner account={} ticket={} reporter={}",
				accountId, ticketId, found->reporter);
			return;
		}

		// On refuse de cancel un ticket deja Resolved (l'admin l'a clos cote GM).
		if (found->state == engine::server::gmtickets::TicketState::Resolved)
		{
			auto pkt = BuildGmTicketCancelResponsePacket(
				static_cast<uint8_t>(GmTicketErrorCode::AlreadyResolved), ticketId,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			LOG_WARN(Net, "[GmTicketHandler] Cancel AlreadyResolved account={} ticket={}",
				accountId, ticketId);
			return;
		}

		// Tolere le double-cancel : Cancel sur un ticket Cancelled retourne Ok
		// (idempotent cote client). Le system::Cancel reset l'etat, le store
		// le reflete avec Update best-effort.
		const bool changed = m_sys->Cancel(ticketId);
		(void)changed;

		if (m_store)
		{
			// Recharge la copie a jour depuis le system (state == Cancelled apres
			// Cancel) et persist. Si l'Update echoue, on logue mais on ne rollback
			// pas : la queue runtime est l'autorite.
			auto updated = m_sys->Find(ticketId);
			if (updated)
			{
				if (!m_store->Update(*updated))
				{
					LOG_WARN(Net, "[GmTicketHandler] Cancel Update failed (account={}, ticket={})",
						accountId, ticketId);
				}
			}
		}

		LOG_INFO(Net, "[GmTicketHandler] Cancel account={} ticket={} ok",
			accountId, ticketId);

		auto pkt = BuildGmTicketCancelResponsePacket(0u, ticketId, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}
}
