// CMANGOS.21 (Phase 5.21 step 3+4) — Implementation ArenaHandler.

#include "src/masterd/handlers/arena/ArenaHandler.h"

#include "src/masterd/session/ConnectionSessionMap.h"
#include "src/masterd/session/SessionManager.h"
#include "src/shared/core/Log.h"
#include "src/shared/network/ArenaPayloads.h"
#include "src/shared/network/NetServer.h"
#include "src/shared/network/ProtocolV1Constants.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace engine::server
{
	namespace
	{
		/// Convertit un uint8 size (2/3/5) en arena::TeamSize enum.
		/// Retourne TeamSize::v2 par defaut sur valeur invalide.
		engine::server::arena::TeamSize ToTeamSize(uint8_t s)
		{
			switch (s)
			{
			case 2: return engine::server::arena::TeamSize::v2;
			case 3: return engine::server::arena::TeamSize::v3;
			case 5: return engine::server::arena::TeamSize::v5;
			default: return engine::server::arena::TeamSize::v2;
			}
		}

		/// Verifie que size est un mode arena valide (2v2 / 3v3 / 5v5).
		bool IsValidArenaSize(uint8_t s)
		{
			return s == 2u || s == 3u || s == 5u;
		}
	}

	void ArenaHandler::HandlePacket(uint32_t connId, uint16_t opcode, uint32_t requestId,
		uint64_t sessionIdHeader,
		const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		if (!m_server || !m_sessionMgr || !m_connMap)
		{
			LOG_WARN(Net, "[ArenaHandler] Drop opcode={} : handler not fully wired", opcode);
			return;
		}

		// Resolution session/account.
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
			const uint8_t kUnauth = static_cast<uint8_t>(ArenaErrorCode::Unauthorized);
			switch (opcode)
			{
			case kOpcodeArenaTeamListRequest:
				pkt = BuildArenaTeamListResponsePacket(kUnauth, {}, requestId, sessionIdHeader);
				break;
			case kOpcodeArenaQueueRequest:
				pkt = BuildArenaQueueResponsePacket(kUnauth, 0u, requestId, sessionIdHeader);
				break;
			case kOpcodeArenaLeaveQueueRequest:
				pkt = BuildArenaLeaveQueueResponsePacket(kUnauth, requestId, sessionIdHeader);
				break;
			case kOpcodeArenaMatchAcceptRequest:
				pkt = BuildArenaMatchAcceptResponsePacket(kUnauth, requestId, sessionIdHeader);
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
		case kOpcodeArenaTeamListRequest:
			HandleTeamList(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeArenaQueueRequest:
			HandleQueue(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeArenaLeaveQueueRequest:
			HandleLeaveQueue(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		case kOpcodeArenaMatchAcceptRequest:
			HandleMatchAccept(connId, requestId, sessionIdHeader, accountId, payload, payloadSize);
			break;
		default:
			break;
		}
	}

	// -------------------------------------------------------------------------

	void ArenaHandler::SeedStarterTeamsIfNeeded(uint64_t accountId)
	{
		using engine::server::arena::ArenaTeam;
		using engine::server::arena::TeamSize;

		auto it = m_accountSeeded.find(accountId);
		if (it != m_accountSeeded.end())
			return;

		// V1 : 3 teams hardcode par account. Note : le registry n'est pas
		// segmente par account, donc les teamIds sont partages — un teamId
		// 1 vu en TeamList par account A est le meme objet que pour account B.
		// Acceptable V1 (sub-PR future avec persistance DB et teamId
		// globalement unique).
		ArenaTeam t1;
		t1.id = kSeedTeam1Id;
		t1.size = TeamSize::v2;
		t1.name = "LCDLLN A";
		t1.rating = kSeedInitialRating;
		m_registry.AddTeam(t1);

		ArenaTeam t2;
		t2.id = kSeedTeam2Id;
		t2.size = TeamSize::v3;
		t2.name = "LCDLLN B";
		t2.rating = kSeedInitialRating;
		m_registry.AddTeam(t2);

		ArenaTeam t3;
		t3.id = kSeedTeam3Id;
		t3.size = TeamSize::v5;
		t3.name = "LCDLLN C";
		t3.rating = kSeedInitialRating;
		m_registry.AddTeam(t3);

		m_accountSeeded[accountId] = true;

		LOG_INFO(Net, "[ArenaHandler] Seeded 3 starter teams for account={}", accountId);
	}

	// -------------------------------------------------------------------------

	void ArenaHandler::HandleTeamList(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		std::vector<ArenaTeamSummary> summaries;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			SeedStarterTeamsIfNeeded(accountId);

			// Enumere les 3 teams seedees pour cet account.
			const uint32_t ids[] = {kSeedTeam1Id, kSeedTeam2Id, kSeedTeam3Id};
			for (uint32_t teamId : ids)
			{
				auto* t = m_registry.Get(teamId);
				if (!t) continue;
				ArenaTeamSummary s;
				s.teamId       = t->id;
				s.size         = static_cast<uint8_t>(t->size);
				s.name         = t->name;
				s.rating       = t->rating;
				s.weeklyGames  = t->weeklyGames;
				s.weeklyWins   = t->weeklyWins;
				summaries.push_back(std::move(s));
			}
		}

		LOG_INFO(Net, "[ArenaHandler] TeamList account={} count={}",
			accountId, summaries.size());

		auto pkt = BuildArenaTeamListResponsePacket(0u, summaries, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------

	void ArenaHandler::HandleQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;

		auto parsed = ParseArenaQueueRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[ArenaHandler] Queue parse failed account={}", accountId);
			auto pkt = BuildArenaQueueResponsePacket(
				static_cast<uint8_t>(ArenaErrorCode::InvalidSize), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		// Validation size : 2 / 3 / 5.
		if (!IsValidArenaSize(parsed->size))
		{
			LOG_INFO(Net, "[ArenaHandler] Queue InvalidSize account={} size={}",
				accountId, static_cast<unsigned>(parsed->size));
			auto pkt = BuildArenaQueueResponsePacket(
				static_cast<uint8_t>(ArenaErrorCode::InvalidSize), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		uint32_t newProposalId = 0;
		bool teamFound = false;
		bool alreadyQueued = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			SeedStarterTeamsIfNeeded(accountId);

			// Verifie team existe.
			auto* t = m_registry.Get(parsed->teamId);
			teamFound = (t != nullptr) && (static_cast<uint8_t>(t->size) == parsed->size);

			if (teamFound)
			{
				// Verifie pas deja en queue.
				if (m_queue.find(accountId) != m_queue.end())
				{
					alreadyQueued = true;
				}
				else
				{
					// Ajoute en queue.
					QueueEntry e;
					e.teamId   = parsed->teamId;
					e.size     = parsed->size;
					e.queuedAt = std::chrono::steady_clock::now();
					m_queue[accountId] = e;

					// V1 : cree immediatement un proposal contre AI fictive.
					newProposalId = m_nextProposalId++;
					Proposal p;
					p.accountId      = accountId;
					p.teamId         = parsed->teamId;
					p.opponentTeamId = 0u; // AI fictive.
					p.opponentName   = kAiOpponentName;
					p.opponentRating = kAiOpponentRating;
					p.createdAt      = std::chrono::steady_clock::now();
					m_proposals[newProposalId] = p;
				}
			}
		}

		if (!teamFound)
		{
			LOG_INFO(Net, "[ArenaHandler] Queue TeamNotFound account={} teamId={} size={}",
				accountId, parsed->teamId, static_cast<unsigned>(parsed->size));
			auto pkt = BuildArenaQueueResponsePacket(
				static_cast<uint8_t>(ArenaErrorCode::TeamNotFound), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (alreadyQueued)
		{
			LOG_INFO(Net, "[ArenaHandler] Queue AlreadyQueued account={}", accountId);
			auto pkt = BuildArenaQueueResponsePacket(
				static_cast<uint8_t>(ArenaErrorCode::AlreadyQueued), 0u,
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[ArenaHandler] Queue OK account={} teamId={} size={} proposalId={}",
			accountId, parsed->teamId, static_cast<unsigned>(parsed->size), newProposalId);

		// Envoie le QueueResponse OK.
		auto pkt = BuildArenaQueueResponsePacket(0u, kV1EstimatedWaitSec,
			requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);

		// V1 : push immediatement le MatchProposal contre AI fictive.
		PushMatchProposal(connId, newProposalId, kAiOpponentName, kAiOpponentRating);
	}

	// -------------------------------------------------------------------------

	void ArenaHandler::HandleLeaveQueue(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* /*payload*/, size_t /*payloadSize*/)
	{
		using namespace engine::network;

		bool wasQueued = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_queue.find(accountId);
			if (it != m_queue.end())
			{
				wasQueued = true;
				m_queue.erase(it);
				// Supprime les proposals associes a cet account.
				for (auto pit = m_proposals.begin(); pit != m_proposals.end(); )
				{
					if (pit->second.accountId == accountId)
						pit = m_proposals.erase(pit);
					else
						++pit;
				}
			}
		}

		if (!wasQueued)
		{
			LOG_INFO(Net, "[ArenaHandler] LeaveQueue NotInQueue account={}", accountId);
			auto pkt = BuildArenaLeaveQueueResponsePacket(
				static_cast<uint8_t>(ArenaErrorCode::NotInQueue),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		LOG_INFO(Net, "[ArenaHandler] LeaveQueue OK account={}", accountId);
		auto pkt = BuildArenaLeaveQueueResponsePacket(0u, requestId, sessionIdHeader);
		if (!pkt.empty())
			m_server->Send(connId, pkt);
	}

	// -------------------------------------------------------------------------

	void ArenaHandler::HandleMatchAccept(uint32_t connId, uint32_t requestId, uint64_t sessionIdHeader,
		uint64_t accountId, const uint8_t* payload, size_t payloadSize)
	{
		using namespace engine::network;
		using engine::server::arena::ApplyEloUpdate;

		auto parsed = ParseArenaMatchAcceptRequestPayload(payload, payloadSize);
		if (!parsed)
		{
			LOG_WARN(Net, "[ArenaHandler] MatchAccept parse failed account={}", accountId);
			auto pkt = BuildArenaMatchAcceptResponsePacket(
				static_cast<uint8_t>(ArenaErrorCode::UnknownProposal),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		bool win = false;
		uint32_t oldRating = 0;
		uint32_t newRating = 0;
		std::string opponentName;
		bool acceptOk = false;
		bool proposalExpired = false;
		bool unknownProposal = false;
		bool acceptValueWasTrue = false;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			auto it = m_proposals.find(parsed->proposalId);
			if (it == m_proposals.end())
			{
				unknownProposal = true;
			}
			else if (it->second.accountId != accountId)
			{
				// Proposal n'appartient pas a cet account.
				unknownProposal = true;
			}
			else
			{
				// Check expiration.
				const auto now = std::chrono::steady_clock::now();
				const auto age = std::chrono::duration_cast<std::chrono::seconds>(
					now - it->second.createdAt).count();
				if (age >= static_cast<long long>(kProposalLifetimeSec))
				{
					proposalExpired = true;
					m_proposals.erase(it);
				}
				else
				{
					// Proposal valide. Recupere l'etat avant de pop.
					const Proposal p = it->second;
					m_proposals.erase(it);
					// Retire l'account de la queue (le match est en cours).
					m_queue.erase(accountId);

					acceptValueWasTrue = parsed->accept;

					if (parsed->accept)
					{
						// V1 : RNG win/loss 50%. Apply ELO update.
						if (!m_rngSeeded)
						{
							const auto seed = static_cast<std::mt19937::result_type>(
								std::chrono::steady_clock::now().time_since_epoch().count());
							m_rng.seed(seed);
							m_rngSeeded = true;
						}
						std::uniform_int_distribution<uint32_t> dist(0u, 1u);
						win = (dist(m_rng) == 1u);

						// Recupere la team du joueur pour l'oldRating.
						auto* myTeam = m_registry.Get(p.teamId);
						oldRating = myTeam ? myTeam->rating : kSeedInitialRating;

						uint32_t winnerNew = 0;
						uint32_t loserNew  = 0;
						if (win)
						{
							ApplyEloUpdate(oldRating, p.opponentRating, 32u, winnerNew, loserNew);
							newRating = winnerNew;
						}
						else
						{
							ApplyEloUpdate(p.opponentRating, oldRating, 32u, winnerNew, loserNew);
							newRating = loserNew;
						}

						// Update la team du joueur.
						if (myTeam)
						{
							myTeam->rating = newRating;
							myTeam->weeklyGames++;
							myTeam->seasonGames++;
							if (win)
							{
								myTeam->weeklyWins++;
								myTeam->seasonWins++;
							}
						}
						opponentName = p.opponentName;
					}
					acceptOk = true;
				}
			}
		}

		if (unknownProposal)
		{
			LOG_INFO(Net, "[ArenaHandler] MatchAccept UnknownProposal account={} proposalId={}",
				accountId, parsed->proposalId);
			auto pkt = BuildArenaMatchAcceptResponsePacket(
				static_cast<uint8_t>(ArenaErrorCode::UnknownProposal),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (proposalExpired)
		{
			LOG_INFO(Net, "[ArenaHandler] MatchAccept ProposalExpired account={} proposalId={}",
				accountId, parsed->proposalId);
			auto pkt = BuildArenaMatchAcceptResponsePacket(
				static_cast<uint8_t>(ArenaErrorCode::ProposalExpired),
				requestId, sessionIdHeader);
			if (!pkt.empty())
				m_server->Send(connId, pkt);
			return;
		}

		if (!acceptOk)
		{
			LOG_WARN(Net, "[ArenaHandler] MatchAccept unexpected state account={}", accountId);
			return;
		}

		LOG_INFO(Net, "[ArenaHandler] MatchAccept OK account={} proposalId={} accept={} win={} old={} new={}",
			accountId, parsed->proposalId,
			acceptValueWasTrue ? 1 : 0,
			win ? 1 : 0,
			oldRating, newRating);

		// ACK Ok.
		auto ack = BuildArenaMatchAcceptResponsePacket(0u, requestId, sessionIdHeader);
		if (!ack.empty())
			m_server->Send(connId, ack);

		// Si accept=true, push le result. Sinon (reject), pas de result push.
		if (acceptValueWasTrue)
		{
			PushMatchResult(connId, win, oldRating, newRating, opponentName);
		}
	}

	// -------------------------------------------------------------------------

	bool ArenaHandler::PushMatchProposal(uint32_t connId, uint32_t proposalId,
		const std::string& opponentTeamName, uint32_t opponentRating)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[ArenaHandler] PushMatchProposal dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[ArenaHandler] PushMatchProposal: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildArenaMatchProposalNotificationPacket(
			proposalId, opponentTeamName, opponentRating, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[ArenaHandler] PushMatchProposal: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[ArenaHandler] PushMatchProposal connId={} proposalId={} opp={} rating={}",
			connId, proposalId, opponentTeamName, opponentRating);
		return true;
	}

	bool ArenaHandler::PushMatchResult(uint32_t connId, bool win, uint32_t oldRating,
		uint32_t newRating, const std::string& opponentName)
	{
		using namespace engine::network;

		if (!m_server || connId == 0u)
		{
			LOG_WARN(Net, "[ArenaHandler] PushMatchResult dropped : server null or connId=0");
			return false;
		}

		const uint64_t sessionIdHeader = FindSessionIdForConn(connId);
		if (sessionIdHeader == 0u)
		{
			LOG_WARN(Net, "[ArenaHandler] PushMatchResult: connId={} no session (skip)", connId);
			return false;
		}

		auto pkt = BuildArenaMatchResultNotificationPacket(
			win, oldRating, newRating, opponentName, sessionIdHeader);
		if (pkt.empty())
		{
			LOG_WARN(Net, "[ArenaHandler] PushMatchResult: build packet failed connId={}", connId);
			return false;
		}

		m_server->Send(connId, pkt);
		LOG_INFO(Net, "[ArenaHandler] PushMatchResult connId={} win={} old={} new={} opp={}",
			connId, win ? 1 : 0, oldRating, newRating, opponentName);
		return true;
	}

	// -------------------------------------------------------------------------

	uint64_t ArenaHandler::FindSessionIdForConn(uint32_t connId) const
	{
		if (!m_connMap) return 0u;
		auto sid = m_connMap->GetSessionId(connId);
		if (!sid) return 0u;
		return *sid;
	}
}
